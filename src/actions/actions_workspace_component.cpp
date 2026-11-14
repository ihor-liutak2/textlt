#include "actions/actions_workspace_component.hpp"

#include <algorithm>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"

namespace textlt::actions {
namespace {

ftxui::Color BorderColor(const Theme& theme) {
    // Same approach as SidebarBorderColor in sidebar_component.cpp.
    if (theme.IsLight()) return ftxui::Color::RGB(70, 70, 70);
    return theme.gutter;
}

constexpr int kMouseWheelScrollItems = 3;
constexpr size_t kItemsScrollbarColumns = 2;
// Mask for items with "hide text": a solid large circle, shown 8 times in tiles.
const std::string kHiddenContentMask = "⬤⬤⬤⬤⬤⬤⬤⬤";

// Same thumb geometry as the notes editor scrollbar.
struct ScrollThumb {
    bool needed = false;
    size_t thumb_height = 0;
    size_t track_space = 0;
    size_t thumb_top = 0;
};

ScrollThumb ComputeScrollThumb(
    size_t visible_rows, size_t visible_items, size_t total_items,
    size_t scroll, size_t max_scroll) {
    ScrollThumb thumb;
    thumb.needed = total_items > visible_items && visible_rows > 0;
    thumb.thumb_height = thumb.needed
        ? std::min(visible_rows, std::max<size_t>(visible_rows >= 2 ? 2 : 1,
              (visible_rows * visible_items) / std::max<size_t>(1, total_items)))
        : visible_rows;
    thumb.track_space = visible_rows > thumb.thumb_height
        ? visible_rows - thumb.thumb_height : 0;
    thumb.thumb_top = thumb.needed && max_scroll > 0 && thumb.track_space > 0
        ? (scroll * thumb.track_space) / max_scroll : 0;
    return thumb;
}

template <typename T>
int IndexById(const std::vector<T>& values, const std::string& id) {
    const auto found = std::find_if(values.begin(), values.end(),
        [&](const T& value) { return value.id == id; });
    return found == values.end() ? -1 : static_cast<int>(std::distance(values.begin(), found));
}

} // namespace

ActionsWorkspaceComponent::ActionsWorkspaceComponent(const Theme* theme,
    StatusCallback status, ClipboardCallback clipboard,
    GroupsChangedCallback groups_changed, std::filesystem::path storage_path)
    : theme_(theme), status_(std::move(status)), clipboard_(std::move(clipboard)),
      groups_changed_(std::move(groups_changed)), repository_(std::move(storage_path)) {
    ftxui::InputOption name_options; name_options.cursor_position = &name_cursor_;
    name_component_ = ftxui::Input(&name_input_, "name", name_options);
    Add(name_component_);
    action_editor_modal_ = std::make_unique<textlt::ActionEditorModal>(
        theme_, [this](const ActionItem& item, std::string& error) {
            std::string created_id;
            const bool saved = item.id.empty()
                ? repository_.CreateItem(
                      selected_group_id_, selected_page_id_, item, created_id, error)
                : repository_.UpdateItem(
                      selected_group_id_, selected_page_id_, item, error);
            if (!saved) return false;
            if (!created_id.empty()) selected_item_id_ = created_id;
            NormalizeSelection();
            if (groups_changed_) groups_changed_();
            Notify(item.id.empty() ? "Action added" : "Action saved");
            TakeFocus();
            return true;
        });
    Add(action_editor_modal_->View());
    std::string error;
    if (!repository_.Load(error)) Notify("Actions load failed: " + error);
    NormalizeSelection();
}

void ActionsWorkspaceComponent::Open() { NormalizeSelection(); TakeFocus(); }
bool ActionsWorkspaceComponent::Save(std::string& error) const { return repository_.Save(error); }

void ActionsWorkspaceComponent::SetProjectContext(
    std::optional<std::pair<std::string, std::string>> project) {
    project_ = std::move(project);
    if (!project_) return;
    std::string id, error;
    if (!repository_.EnsureProjectGroup(project_->first, project_->second, id, error)) {
        Notify("Actions project group failed: " + error); return;
    }
    if (groups_changed_) groups_changed_();
}

const ActionGroup* ActionsWorkspaceComponent::SelectedGroup() const {
    const auto& groups = repository_.Data().groups;
    const int index = IndexById(groups, selected_group_id_);
    return index < 0 ? nullptr : &groups[static_cast<size_t>(index)];
}
const ActionPage* ActionsWorkspaceComponent::SelectedPage() const {
    const ActionGroup* group = SelectedGroup(); if (!group) return nullptr;
    const int index = IndexById(group->pages, selected_page_id_);
    return index < 0 ? nullptr : &group->pages[static_cast<size_t>(index)];
}
const ActionItem* ActionsWorkspaceComponent::SelectedItem() const {
    const ActionPage* page = SelectedPage(); if (!page) return nullptr;
    const int index = IndexById(page->items, selected_item_id_);
    return index < 0 ? nullptr : &page->items[static_cast<size_t>(index)];
}

void ActionsWorkspaceComponent::NormalizeSelection() {
    const auto& groups = repository_.Data().groups;
    if (IndexById(groups, selected_group_id_) < 0)
        selected_group_id_ = groups.empty() ? "" : groups.front().id;
    const ActionGroup* group = SelectedGroup();
    if (!group || IndexById(group->pages, selected_page_id_) < 0)
        selected_page_id_ = !group || group->pages.empty() ? "" : group->pages.front().id;
    const ActionPage* page = SelectedPage();
    if (!page || IndexById(page->items, selected_item_id_) < 0)
        selected_item_id_ = !page || page->items.empty() ? "" : page->items.front().id;
}

void ActionsWorkspaceComponent::SelectGroup(const std::string& id) {
    if (IndexById(repository_.Data().groups, id) < 0) return;
    selected_group_id_ = id; selected_page_id_.clear(); selected_item_id_.clear();
    page_scroll_ = item_scroll_ = 0; NormalizeSelection(); focus_ = FocusArea::Pages;
}

void ActionsWorkspaceComponent::Notify(const std::string& message) { if (status_) status_(message); }
void ActionsWorkspaceComponent::OpenNameDialog(Dialog dialog, std::string value) {
    dialog_ = dialog; name_input_ = std::move(value); name_cursor_ = static_cast<int>(name_input_.size()); name_component_->TakeFocus();
}
void ActionsWorkspaceComponent::CloseDialog() { dialog_ = Dialog::None; TakeFocus(); }
void ActionsWorkspaceComponent::AfterMutation(const std::string& message) {
    NormalizeSelection(); if (groups_changed_) groups_changed_(); Notify(message); CloseDialog();
}

void ActionsWorkspaceComponent::NewGroup() { OpenNameDialog(Dialog::NewGroup); }
void ActionsWorkspaceComponent::RenameGroup() { if (const auto* g = SelectedGroup()) OpenNameDialog(Dialog::RenameGroup, g->name); }
void ActionsWorkspaceComponent::DeleteGroup() { if (SelectedGroup()) dialog_ = Dialog::DeleteGroup; }
void ActionsWorkspaceComponent::NewPage() { if (SelectedGroup()) OpenNameDialog(Dialog::NewPage); }
void ActionsWorkspaceComponent::RenamePage() { if (const auto* p = SelectedPage()) OpenNameDialog(Dialog::RenamePage, p->name); }
void ActionsWorkspaceComponent::DeletePage() { if (SelectedPage()) dialog_ = Dialog::DeletePage; }
void ActionsWorkspaceComponent::AddAction() {
    if (SelectedPage() && action_editor_modal_) action_editor_modal_->Open();
}
void ActionsWorkspaceComponent::EditAction() {
    if (const auto* item = SelectedItem(); item && action_editor_modal_)
        action_editor_modal_->Open(item);
}
void ActionsWorkspaceComponent::DeleteAction() { if (SelectedItem()) dialog_ = Dialog::DeleteAction; }

void ActionsWorkspaceComponent::MovePage(int delta) {
    std::string error; if (!repository_.MovePage(selected_group_id_, selected_page_id_, delta, error)) Notify("Page move failed: " + error); else AfterMutation("Page moved");
}
void ActionsWorkspaceComponent::MoveAction(int delta) {
    std::string error; if (!repository_.MoveItem(selected_group_id_, selected_page_id_, selected_item_id_, delta, error)) Notify("Action move failed: " + error); else AfterMutation("Action moved");
}
void ActionsWorkspaceComponent::CopySelectedAction() {
    const ActionItem* item = SelectedItem(); if (!item) { Notify("No action selected"); return; }
    Notify(clipboard_ && clipboard_(item->content) ? "Copied action: " + item->title : "Could not copy action to the system clipboard");
}
void ActionsWorkspaceComponent::CopyAllActions() {
    const ActionPage* page = SelectedPage(); if (!page || page->items.empty()) { Notify("No actions to copy"); return; }
    std::string text;
    for (size_t i = 0; i < page->items.size(); ++i) { if (i) text += "\n\n"; text += page->items[i].content; }
    Notify(clipboard_ && clipboard_(text) ? "Copied all actions" : "Could not copy actions to the system clipboard");
}

bool ActionsWorkspaceComponent::HasSelectedPage() const {
    return SelectedPage() != nullptr;
}

bool ActionsWorkspaceComponent::ImportActions(
    std::vector<ActionItem> items, std::string& error) {
    const size_t count = items.size();
    if (!repository_.AppendItems(
            selected_group_id_, selected_page_id_, std::move(items), error)) {
        return false;
    }
    NormalizeSelection();
    const ActionPage* page = SelectedPage();
    if (page && page->items.size() >= count) {
        selected_item_id_ = page->items[page->items.size() - count].id;
    }
    item_scroll_ = page && page->items.size() > count
        ? page->items.size() - count
        : 0;
    if (groups_changed_) groups_changed_();
    Notify("Imported " + std::to_string(count) +
        (count == 1 ? " action" : " actions"));
    return true;
}

void ActionsWorkspaceComponent::AcceptDialog() {
    std::string id, error;
    switch (dialog_) {
        case Dialog::NewGroup:
            if (repository_.CreateGroup(name_input_, id, error)) { selected_group_id_ = id; AfterMutation("Group created"); } break;
        case Dialog::RenameGroup:
            if (repository_.RenameGroup(selected_group_id_, name_input_, error)) AfterMutation("Group renamed"); break;
        case Dialog::NewPage:
            if (repository_.CreatePage(selected_group_id_, name_input_, id, error)) { selected_page_id_ = id; AfterMutation("Page created"); } break;
        case Dialog::RenamePage:
            if (repository_.RenamePage(selected_group_id_, selected_page_id_, name_input_, error)) AfterMutation("Page renamed"); break;
        case Dialog::DeleteGroup:
            if (repository_.DeleteGroup(selected_group_id_, error)) { selected_group_id_.clear(); AfterMutation("Group deleted"); } break;
        case Dialog::DeletePage: {
            const bool permanent = SelectedGroup() && SelectedGroup()->trash;
            if (repository_.SoftDeletePage(selected_group_id_, selected_page_id_, error)) {
                selected_page_id_.clear();
                AfterMutation(permanent ? "Page deleted permanently" : "Page moved to Deleted");
            }
            break;
        }
        case Dialog::DeleteAction:
            if (repository_.DeleteItem(selected_group_id_, selected_page_id_, selected_item_id_, error)) { selected_item_id_.clear(); AfterMutation("Action deleted"); } break;
        case Dialog::None: return;
    }
    if (!error.empty()) Notify(error);
}

ftxui::Element ActionsWorkspaceComponent::RenderPages() {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    if (pages_view_box_.x_max > pages_view_box_.x_min ||
        pages_view_box_.y_max > pages_view_box_.y_min) {
        pages_view_known_ = true;
    }
    if (pages_view_known_) {
        size_t visible_count = 0;
        for (const auto& box : page_boxes_) {
            if (box.y_max < pages_view_box_.y_min || box.y_min > pages_view_box_.y_max) break;
            ++visible_count;
        }
        if (visible_count > 0) pages_visible_count_ = std::max<size_t>(1, visible_count);
    }
    Elements rows; page_boxes_.clear(); const ActionGroup* group = SelectedGroup();
    const size_t total_pages = group ? group->pages.size() : 0;
    const size_t visible_pages = pages_visible_count_ ? pages_visible_count_ : total_pages;
    const size_t max_scroll = total_pages > visible_pages ? total_pages - visible_pages : 0;
    if (page_scroll_ > max_scroll) page_scroll_ = max_scroll;
    if (!group) rows.push_back(paragraph("Create or select an Actions group in the Sidebar.") | dim | color(theme.foreground));
    else if (group->pages.empty()) rows.push_back(paragraph("No pages. Use Actions > New Page.") | dim | color(theme.foreground));
    else {
        const int selected = IndexById(group->pages, selected_page_id_);
        const size_t start = std::min(page_scroll_, group->pages.size() - 1);
        // reflect() holds a Box& across layout: size the storage before binding
        // so later growth cannot move the boxes out from under the references.
        page_boxes_.assign(group->pages.size() - start, {});
        for (size_t i = start; i < group->pages.size(); ++i) {
            const bool is_selected = static_cast<int>(i) == selected;
            Element row = paragraph(group->pages[i].name) |
                color(is_selected ? theme.modal_selected_item_fg : theme.foreground) |
                reflect(page_boxes_[i - start]);
            if (is_selected) row = row | bgcolor(theme.modal_selected_item_bg);
            rows.push_back(row); rows.push_back(separator() | color(BorderColor(theme)));
        }
    }
    return vbox({text(" Pages ") | bold | color(theme.foreground), separator() | color(BorderColor(theme)),
        vbox(std::move(rows)) | frame | yflex | reflect(pages_view_box_)}) |
        size(WIDTH, EQUAL, 26) | borderStyled(ftxui::LIGHT, BorderColor(theme)) | reflect(pages_box_);
}

ftxui::Element ActionsWorkspaceComponent::RenderItems() {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    if (items_view_box_.x_max > items_view_box_.x_min ||
        items_view_box_.y_max > items_view_box_.y_min) {
        items_view_known_ = true;
    }
    size_t visible_rows = 0;
    if (items_view_known_) {
        visible_rows = static_cast<size_t>(
            std::max(0, items_view_box_.y_max - items_view_box_.y_min + 1));
        size_t visible_count = 0;
        for (const auto& box : item_boxes_) {
            if (box.y_max < items_view_box_.y_min || box.y_min > items_view_box_.y_max) break;
            ++visible_count;
        }
        if (visible_count > 0) items_visible_count_ = std::max<size_t>(1, visible_count);
    }
    items_visible_rows_ = visible_rows;

    Elements cards; item_boxes_.clear(); copy_boxes_.clear(); const ActionPage* page = SelectedPage();
    std::string header = page ? page->name : "Actions";
    const size_t total_items = page ? page->items.size() : 0;
    const size_t visible_items = items_visible_count_ ? items_visible_count_ : total_items;
    const size_t max_scroll = total_items > visible_items ? total_items - visible_items : 0;
    if (item_scroll_ > max_scroll) item_scroll_ = max_scroll;
    if (!page) cards.push_back(text(" No page selected") | dim | color(theme.foreground));
    else if (page->items.empty()) cards.push_back(text(" No actions on this page") | dim | color(theme.foreground));
    else {
        const int selected = IndexById(page->items, selected_item_id_);
        const size_t start = std::min(item_scroll_, page->items.size() - 1);
        // Same stable-address requirement for reflect() as in RenderPages.
        item_boxes_.assign(page->items.size() - start, {});
        copy_boxes_.assign(page->items.size() - start, {});
        for (size_t i = start; i < page->items.size(); ++i) {
            const auto& item = page->items[i];
            const bool is_selected = static_cast<int>(i) == selected;
            Elements body = {hbox({
                text(" " + item.title) | bold | xflex,
                item.description.empty()
                    ? emptyElement()
                    : text(" " + item.description) | dim | align_right,
                text("          "),
                hbox({
                    separator() | color(BorderColor(theme)),
                    text(" Copy "),
                }) | reflect(copy_boxes_[i - start])}) |
                bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg)};
            body.push_back(item.hide_text
                ? hbox({text("     "), text(kHiddenContentMask) |
                    color(is_selected ? theme.modal_accent : theme.foreground)})
                : hbox({text("     "),
                    paragraph(item.content) |
                        color(is_selected ? theme.modal_accent : theme.foreground) | xflex}));
            Element card = vbox(std::move(body)) | reflect(item_boxes_[i - start]);
            cards.push_back(card);
            if (i + 1 < page->items.size()) cards.push_back(text(""));
        }
    }
    Element title_row = hbox({
        text(" " + header) | bold | color(theme.foreground) | xflex,
        text("          "),
        hbox({
            separator() | color(BorderColor(theme)),
            text(" Add Action ") | color(theme.foreground),
        }) | reflect(add_action_box_),
    });
    Element list = hbox({
        vbox(std::move(cards)) | frame | xflex,
        RenderItemsScrollbar(
            visible_rows, total_items, visible_items, item_scroll_, max_scroll, theme),
    }) | yflex | reflect(items_view_box_);
    return vbox({title_row, separator() | color(BorderColor(theme)), list}) |
        xflex | borderStyled(ftxui::LIGHT, BorderColor(theme)) | reflect(items_box_);
}

ftxui::Element ActionsWorkspaceComponent::RenderItemsScrollbar(
    size_t visible_rows, size_t total_items, size_t visible_items, size_t scroll,
    size_t max_scroll, const Theme& theme) {
    using namespace ftxui;
    if (visible_rows == 0) return emptyElement();
    const ScrollThumb thumb = ComputeScrollThumb(
        visible_rows, visible_items, total_items, scroll, max_scroll);
    Elements cells;
    cells.reserve(visible_rows);
    for (size_t row = 0; row < visible_rows; ++row) {
        if (!thumb.needed) {
            cells.push_back(text(std::string(kItemsScrollbarColumns, ' ')) |
                bgcolor(theme.background));
        } else if (row >= thumb.thumb_top && row < thumb.thumb_top + thumb.thumb_height) {
            cells.push_back(text(std::string(kItemsScrollbarColumns, ' ')) |
                bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg));
        } else {
            cells.push_back(text("│ ") | color(theme.gutter) | dim);
        }
    }
    return vbox(std::move(cells)) | reflect(items_scrollbar_box_);
}

void ActionsWorkspaceComponent::ScrollItems(int delta) {
    const ActionPage* page = SelectedPage();
    if (!page || page->items.empty()) return;
    const size_t total_items = page->items.size();
    const size_t visible_items = items_visible_count_ ? items_visible_count_ : total_items;
    const size_t max_scroll = total_items > visible_items ? total_items - visible_items : 0;
    const long long target = static_cast<long long>(item_scroll_) + delta;
    item_scroll_ = static_cast<size_t>(
        std::clamp<long long>(target, 0, static_cast<long long>(max_scroll)));
}

ftxui::Element ActionsWorkspaceComponent::RenderDialog() {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    if (dialog_ == Dialog::None) return emptyElement();
    const bool confirm = dialog_ == Dialog::DeleteGroup || dialog_ == Dialog::DeletePage || dialog_ == Dialog::DeleteAction;
    Element body;
    if (confirm) {
        std::string question = " Confirm deletion? ";
        if (dialog_ == Dialog::DeletePage) {
            const ActionGroup* group = SelectedGroup();
            question = group && group->trash ? " Delete permanently? " : " Move page to Deleted? ";
        }
        body = vbox({text(question) | bold | color(theme.modal_text_color),
            text(" Enter: delete   Esc: cancel ") | color(theme.modal_text_color)});
    }
    else body = vbox({text(" Name ") | bold | color(theme.modal_text_color), name_component_->Render(),
        text(" Enter: save   Esc: cancel ") | dim | color(theme.modal_text_color)});
    return body | size(WIDTH, GREATER_THAN, 48) | borderStyled(ftxui::LIGHT, BorderColor(theme)) | bgcolor(theme.menu_background) | clear_under | center;
}

ftxui::Element ActionsWorkspaceComponent::OnRender() {
    using namespace ftxui; Element base = hbox({RenderPages(), RenderItems()}) | flex;
    if (action_editor_modal_ && action_editor_modal_->IsOpen())
        return dbox({base, action_editor_modal_->View()->Render() | clear_under | center});
    return dialog_ == Dialog::None ? base : dbox({base, RenderDialog()});
}

ftxui::Component ActionsWorkspaceComponent::ActiveChild() {
    if (action_editor_modal_ && action_editor_modal_->IsOpen())
        return action_editor_modal_->View();
    if (dialog_ != Dialog::None) return name_component_;
    return nullptr;
}

bool ActionsWorkspaceComponent::OnEvent(ftxui::Event event) {
    if (action_editor_modal_ && action_editor_modal_->IsOpen())
        return action_editor_modal_->OnEvent(std::move(event));
    if (dialog_ != Dialog::None) {
        if (event == ftxui::Event::Escape) { CloseDialog(); return true; }
        const bool confirm = dialog_ == Dialog::DeleteGroup || dialog_ == Dialog::DeletePage || dialog_ == Dialog::DeleteAction;
        if (confirm && event == ftxui::Event::Return) { AcceptDialog(); return true; }
        if (event == ftxui::Event::Return) { AcceptDialog(); return true; }
        return name_component_->OnEvent(event);
    }
    const ActionGroup* group = SelectedGroup(); const ActionPage* page = SelectedPage();
    if (event == ftxui::Event::Tab) { focus_ = focus_ == FocusArea::Pages ? FocusArea::Items : FocusArea::Pages; return true; }
    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::ArrowUp) {
        const int delta = event == ftxui::Event::ArrowDown ? 1 : -1;
        if (focus_ == FocusArea::Pages && group && !group->pages.empty()) {
            int index = std::max(0, IndexById(group->pages, selected_page_id_)); index = std::clamp(index + delta, 0, static_cast<int>(group->pages.size()) - 1);
            selected_page_id_ = group->pages[index].id; selected_item_id_.clear(); NormalizeSelection();
            const size_t selected_index = static_cast<size_t>(index);
            if (selected_index < page_scroll_) page_scroll_ = selected_index;
            else if (pages_visible_count_ > 0 && selected_index >= page_scroll_ + pages_visible_count_)
                page_scroll_ = selected_index - pages_visible_count_ + 1;
            item_scroll_ = 0;
        } else if (focus_ == FocusArea::Items && page && !page->items.empty()) {
            int index = std::max(0, IndexById(page->items, selected_item_id_)); index = std::clamp(index + delta, 0, static_cast<int>(page->items.size()) - 1);
            selected_item_id_ = page->items[index].id;
            const size_t selected_index = static_cast<size_t>(index);
            if (selected_index < item_scroll_) item_scroll_ = selected_index;
            else if (items_visible_count_ > 0 && selected_index >= item_scroll_ + items_visible_count_)
                item_scroll_ = selected_index - items_visible_count_ + 1;
        }
        return true;
    }
    if (event == ftxui::Event::Return && focus_ == FocusArea::Items) { CopySelectedAction(); return true; }
    if (event.is_mouse()) {
        const ftxui::Mouse mouse = event.mouse();
        const size_t total_items = page ? page->items.size() : 0;
        const size_t visible_items = items_visible_count_ ? items_visible_count_ : total_items;
        const size_t max_scroll = total_items > visible_items ? total_items - visible_items : 0;
        if (mouse.button == ftxui::Mouse::WheelUp && items_view_box_.Contain(mouse.x, mouse.y)) { ScrollItems(-kMouseWheelScrollItems); return true; }
        if (mouse.button == ftxui::Mouse::WheelDown && items_view_box_.Contain(mouse.x, mouse.y)) { ScrollItems(kMouseWheelScrollItems); return true; }
        if (items_scrollbar_dragging_ && mouse.motion == ftxui::Mouse::Released) { items_scrollbar_dragging_ = false; return true; }
        if (items_scrollbar_dragging_ && mouse.motion == ftxui::Mouse::Moved) {
            if (total_items > visible_items && items_visible_rows_ > 0) {
                const ScrollThumb thumb = ComputeScrollThumb(items_visible_rows_, visible_items, total_items, 0, 0);
                if (thumb.track_space > 0) {
                    const long long delta = static_cast<long long>(mouse.y - items_scrollbar_drag_y_) *
                        static_cast<long long>(max_scroll) / static_cast<long long>(thumb.track_space);
                    item_scroll_ = static_cast<size_t>(std::clamp<long long>(
                        static_cast<long long>(items_scrollbar_drag_scroll_) + delta,
                        0, static_cast<long long>(max_scroll)));
                }
            }
            return true;
        }
        if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            if (add_action_box_.Contain(mouse.x, mouse.y)) { AddAction(); return true; }
            if (items_scrollbar_box_.Contain(mouse.x, mouse.y)) {
                if (total_items > visible_items && items_visible_rows_ > 0) {
                    const ScrollThumb thumb = ComputeScrollThumb(items_visible_rows_, visible_items, total_items, item_scroll_, max_scroll);
                    if (thumb.track_space > 0) {
                        const int relative_y = std::clamp(mouse.y - items_scrollbar_box_.y_min, 0,
                            static_cast<int>(items_visible_rows_) - 1);
                        const int thumb_top = std::clamp(
                            relative_y - static_cast<int>(thumb.thumb_height / 2), 0,
                            static_cast<int>(thumb.track_space));
                        item_scroll_ = (static_cast<size_t>(thumb_top) * max_scroll) / thumb.track_space;
                        items_scrollbar_dragging_ = true;
                        items_scrollbar_drag_y_ = mouse.y;
                        items_scrollbar_drag_scroll_ = item_scroll_;
                    }
                }
                return true;
            }
            for (size_t i = 0; i < page_boxes_.size(); ++i) if (page_boxes_[i].Contain(mouse.x, mouse.y) && group && !group->pages.empty()) { selected_page_id_ = group->pages[std::min(page_scroll_, group->pages.size() - 1) + i].id; selected_item_id_.clear(); NormalizeSelection(); focus_ = FocusArea::Pages; return true; }
            for (size_t i = 0; i < item_boxes_.size(); ++i) if (item_boxes_[i].Contain(mouse.x, mouse.y) && page && !page->items.empty()) { selected_item_id_ = page->items[std::min(item_scroll_, page->items.size() - 1) + i].id; focus_ = FocusArea::Items; if (copy_boxes_[i].Contain(mouse.x, mouse.y)) CopySelectedAction(); return true; }
        }
    }
    return false;
}

} // namespace textlt::actions
