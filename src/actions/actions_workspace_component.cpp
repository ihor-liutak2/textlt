#include "actions/actions_workspace_component.hpp"

#include <algorithm>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"

namespace textlt::actions {
namespace {

ftxui::Color BorderColor(const Theme& theme) { return theme.gutter; }

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
        case Dialog::DeletePage:
            if (repository_.DeletePage(selected_group_id_, selected_page_id_, error)) { selected_page_id_.clear(); AfterMutation("Page deleted"); } break;
        case Dialog::DeleteAction:
            if (repository_.DeleteItem(selected_group_id_, selected_page_id_, selected_item_id_, error)) { selected_item_id_.clear(); AfterMutation("Action deleted"); } break;
        case Dialog::None: return;
    }
    if (!error.empty()) Notify(error);
}

ftxui::Element ActionsWorkspaceComponent::RenderPages() {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows; page_boxes_.clear(); const ActionGroup* group = SelectedGroup();
    if (!group) rows.push_back(paragraph("Create or select an Actions group in the Sidebar.") | dim);
    else if (group->pages.empty()) rows.push_back(paragraph("No pages. Use Actions > New Page.") | dim);
    else {
        const int selected = IndexById(group->pages, selected_page_id_);
        const size_t start = std::min(page_scroll_, group->pages.size() - 1);
        // reflect() holds a Box& across layout: size the storage before binding
        // so later growth cannot move the boxes out from under the references.
        page_boxes_.assign(group->pages.size() - start, {});
        for (size_t i = start; i < group->pages.size(); ++i) {
            Element row = paragraph(group->pages[i].name) | flex | reflect(page_boxes_[i - start]);
            if (static_cast<int>(i) == selected) row = row | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg);
            rows.push_back(row); rows.push_back(separator() | color(BorderColor(theme)));
        }
    }
    return vbox({text(" Pages ") | bold, separator(), vbox(std::move(rows)) | frame | yflex}) |
        size(WIDTH, EQUAL, 26) | border | reflect(pages_box_);
}

ftxui::Element ActionsWorkspaceComponent::RenderItems() {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements cards; item_boxes_.clear(); copy_boxes_.clear(); const ActionPage* page = SelectedPage();
    std::string header = page ? page->name : "Actions";
    if (!page) cards.push_back(text(" No page selected") | dim);
    else if (page->items.empty()) cards.push_back(text(" No actions on this page") | dim);
    else {
        const int selected = IndexById(page->items, selected_item_id_);
        const size_t start = std::min(item_scroll_, page->items.size() - 1);
        // Same stable-address requirement for reflect() as in RenderPages.
        item_boxes_.assign(page->items.size() - start, {});
        copy_boxes_.assign(page->items.size() - start, {});
        for (size_t i = start; i < page->items.size(); ++i) {
            const auto& item = page->items[i];
            Elements body = {hbox({text(" " + item.title) | bold | xflex,
                text(" Copy ") | border | reflect(copy_boxes_[i - start])})};
            if (!item.description.empty()) body.push_back(paragraph(" " + item.description) | dim);
            body.push_back(separator()); body.push_back(paragraph(item.content));
            Element card = vbox(std::move(body)) | border | reflect(item_boxes_[i - start]);
            if (static_cast<int>(i) == selected) card = card | color(theme.modal_selected_item_fg);
            cards.push_back(card);
        }
    }
    return vbox({paragraph(" " + header) | bold, separator(), vbox(std::move(cards)) | frame | yflex}) |
        xflex | border | reflect(items_box_);
}

ftxui::Element ActionsWorkspaceComponent::RenderDialog() {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    if (dialog_ == Dialog::None) return emptyElement();
    const bool confirm = dialog_ == Dialog::DeleteGroup || dialog_ == Dialog::DeletePage || dialog_ == Dialog::DeleteAction;
    Element body;
    if (confirm) body = vbox({text(" Confirm deletion? ") | bold, text(" Enter: delete   Esc: cancel ")});
    else body = vbox({text(" Name ") | bold, name_component_->Render(), text(" Enter: save   Esc: cancel ") | dim});
    return body | size(WIDTH, GREATER_THAN, 48) | border | bgcolor(theme.menu_background) | clear_under | center;
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
            selected_page_id_ = group->pages[index].id; selected_item_id_.clear(); NormalizeSelection(); page_scroll_ = static_cast<size_t>(index); item_scroll_ = 0;
        } else if (focus_ == FocusArea::Items && page && !page->items.empty()) {
            int index = std::max(0, IndexById(page->items, selected_item_id_)); index = std::clamp(index + delta, 0, static_cast<int>(page->items.size()) - 1);
            selected_item_id_ = page->items[index].id; item_scroll_ = static_cast<size_t>(index);
        }
        return true;
    }
    if (event == ftxui::Event::Return && focus_ == FocusArea::Items) { CopySelectedAction(); return true; }
    if (event.is_mouse() && event.mouse().button == ftxui::Mouse::Left && event.mouse().motion == ftxui::Mouse::Pressed) {
        for (size_t i = 0; i < page_boxes_.size(); ++i) if (page_boxes_[i].Contain(event.mouse().x, event.mouse().y) && group && !group->pages.empty()) { selected_page_id_ = group->pages[std::min(page_scroll_, group->pages.size() - 1) + i].id; selected_item_id_.clear(); NormalizeSelection(); focus_ = FocusArea::Pages; return true; }
        for (size_t i = 0; i < item_boxes_.size(); ++i) if (item_boxes_[i].Contain(event.mouse().x, event.mouse().y) && page && !page->items.empty()) { selected_item_id_ = page->items[std::min(item_scroll_, page->items.size() - 1) + i].id; focus_ = FocusArea::Items; if (copy_boxes_[i].Contain(event.mouse().x, event.mouse().y)) CopySelectedAction(); return true; }
    }
    return false;
}

} // namespace textlt::actions
