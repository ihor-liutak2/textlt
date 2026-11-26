#include "actions/actions_workspace_component.hpp"

#include "actions/action_snapshot.hpp"
#include "editor/clipboard_controller.hpp"
#include <algorithm>
#include <cctype>
#include <exception>

#include "ftxui/component/animation.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ui_button.hpp"
#include "ui_selectable_input.hpp"

namespace textlt::actions {
namespace {

ftxui::Color BorderColor(const Theme& theme) {
    // Same approach as SidebarBorderColor in sidebar_component.cpp.
    if (theme.IsLight()) return ftxui::Color::RGB(70, 70, 70);
    return theme.gutter;
}

constexpr int kMouseWheelScrollItems = 3;
constexpr int kMouseWheelScrollNavigator = 3;
constexpr size_t kItemsScrollbarColumns = 2;
constexpr int kActionContentPreviewRows = 5;
constexpr int kActionActiveMarkerWidth = 2;
constexpr size_t kActionMetadataFirstRowCharacters = 25;
constexpr auto kCopyCaptionFlashDuration = std::chrono::milliseconds(400);
// Mask for items with "hide text": a solid large circle, shown 8 times in tiles.
const std::string kHiddenContentMask = "⬤⬤⬤⬤⬤⬤⬤⬤";

class ActionEndMarkerNode final : public ftxui::Node {
public:
    ActionEndMarkerNode(ftxui::Element content, bool active,
        ftxui::Color color, ftxui::Color title_background,
        std::shared_ptr<std::vector<int>> header_rows, size_t header_index)
        : ftxui::Node({std::move(content)}), active_(active), color_(color),
          title_background_(title_background),
          header_rows_(std::move(header_rows)), header_index_(header_index) {}

    void ComputeRequirement() override {
        children_.front()->ComputeRequirement();
        requirement_ = children_.front()->requirement();
        requirement_.min_x += kActionActiveMarkerWidth;
    }

    void SetBox(ftxui::Box box) override {
        ftxui::Node::SetBox(box);
        ftxui::Box content_box = box;
        content_box.x_max -= kActionActiveMarkerWidth;
        children_.front()->SetBox(content_box);
        if (header_index_ < header_rows_->size())
            (*header_rows_)[header_index_] = box.y_min;
    }

    void Render(ftxui::Screen& screen) override {
        children_.front()->Render(screen);
        // Continue the title-row background through the marker column, so it
        // reaches the card's right edge instead of stopping at the content.
        for (int x = box_.x_max - kActionActiveMarkerWidth + 1;
             x <= box_.x_max; ++x) {
            if (!screen.stencil.Contain(x, box_.y_min)) continue;
            screen.CellAt(x, box_.y_min).background_color = title_background_;
        }
        if (!active_) return;
        for (int y = box_.y_min; y <= box_.y_max; ++y) {
            for (int x = box_.x_max - kActionActiveMarkerWidth + 1;
                 x <= box_.x_max; ++x) {
                if (!screen.stencil.Contain(x, y)) continue;
                ftxui::Cell& cell = screen.CellAt(x, y);
                cell.character = "█";
                cell.foreground_color = color_;
            }
        }
    }

private:
    bool active_ = false;
    ftxui::Color color_;
    ftxui::Color title_background_;
    std::shared_ptr<std::vector<int>> header_rows_;
    size_t header_index_ = 0;
};

ftxui::Element WithActionEndMarker(
    ftxui::Element content, bool active, ftxui::Color color,
    ftxui::Color title_background,
    const std::shared_ptr<std::vector<int>>& header_rows, size_t header_index) {
    return std::make_shared<ActionEndMarkerNode>(
        std::move(content), active, color, title_background, header_rows,
        header_index);
}

class HeaderBackgroundNode final : public ftxui::Node {
public:
    HeaderBackgroundNode(ftxui::Element scrollbar,
        std::shared_ptr<std::vector<int>> header_rows, ftxui::Color color)
        : ftxui::Node({std::move(scrollbar)}),
          header_rows_(std::move(header_rows)), color_(color) {}

    void ComputeRequirement() override {
        children_.front()->ComputeRequirement();
        requirement_ = children_.front()->requirement();
    }

    void SetBox(ftxui::Box box) override {
        ftxui::Node::SetBox(box);
        children_.front()->SetBox(box);
    }

    void Render(ftxui::Screen& screen) override {
        children_.front()->Render(screen);
        for (const int y : *header_rows_) {
            if (y < box_.y_min || y > box_.y_max) continue;
            for (int x = box_.x_min; x <= box_.x_max; ++x) {
                if (screen.stencil.Contain(x, y))
                    screen.CellAt(x, y).background_color = color_;
            }
        }
    }

private:
    std::shared_ptr<std::vector<int>> header_rows_;
    ftxui::Color color_;
};

ftxui::Element ExtendHeaderBackground(ftxui::Element scrollbar,
    const std::shared_ptr<std::vector<int>>& header_rows, ftxui::Color color) {
    return std::make_shared<HeaderBackgroundNode>(
        std::move(scrollbar), header_rows, color);
}

bool IsWordCharacter(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) != 0 || character == '_';
}

std::string LowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

std::pair<size_t, size_t> LineBounds(const std::string& text, size_t offset) {
    offset = std::min(offset, text.size());
    const size_t start = text.rfind('\n', offset == 0 ? 0 : offset - 1);
    const size_t line_start = start == std::string::npos ? 0 : start + 1;
    size_t line_end = text.find('\n', offset);
    if (line_end == std::string::npos) line_end = text.size();
    return {line_start, line_end};
}

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

ftxui::Element RenderHighlightedLine(const Theme& theme,
    const std::string& line, size_t line_offset, size_t match_start,
    size_t match_length) {
    using namespace ftxui;
    Elements parts;
    parts.push_back(ftxui::text(" "));
    const bool overlaps = match_length > 0 &&
        match_start < line_offset + line.size() &&
        match_start + match_length > line_offset;
    if (!overlaps) {
        parts.push_back(ftxui::text(line));
        return hbox(std::move(parts));
    }
    const size_t begin = std::max(match_start, line_offset) - line_offset;
    const size_t end = std::min(match_start + match_length,
        line_offset + line.size()) - line_offset;
    if (begin > 0) parts.push_back(ftxui::text(line.substr(0, begin)));
    parts.push_back(ftxui::text(line.substr(begin, end - begin)) |
        bgcolor(theme.active_match_bg) | color(theme.selection_fg));
    if (end < line.size()) parts.push_back(ftxui::text(line.substr(end)));
    return hbox(std::move(parts));
}

template <typename T>
int IndexById(const std::vector<T>& values, const std::string& id) {
    const auto found = std::find_if(values.begin(), values.end(),
        [&](const T& value) { return value.id == id; });
    return found == values.end() ? -1 : static_cast<int>(std::distance(values.begin(), found));
}

std::string BaseGroupName(const std::string& name) {
    // Strip a trailing " (N)" suffix so "Foo" and "Foo (1)" share base "Foo".
    const size_t paren = name.rfind(" (");
    if (paren == std::string::npos) return name;
    const size_t end = name.size();
    if (end > 0 && name[end - 1] == ')') {
        const std::string inner = name.substr(paren + 2, end - paren - 3);
        if (!inner.empty() &&
            std::all_of(inner.begin(), inner.end(),
                [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            return name.substr(0, paren);
        }
    }
    return name;
}

bool HasNumberedSuffix(const std::string& name) {
    return BaseGroupName(name) != name;
}

} // namespace

ActionsWorkspaceComponent::ActionsWorkspaceComponent(const Theme* theme,
    StatusCallback status, ClipboardCallback clipboard,
    GroupsChangedCallback groups_changed, std::filesystem::path storage_path)
    : theme_(theme), status_(std::move(status)), clipboard_(std::move(clipboard)),
      groups_changed_(std::move(groups_changed)), repository_(std::move(storage_path)),
      context_menu_(theme) {
    name_selection_ = std::make_shared<InputSelection>();
    name_component_ = MakeSelectableInput(name_input_, name_selection_->cursor, "name",
        ftxui::InputOption{}, name_selection_);
    Add(name_component_);
    action_editor_modal_ = std::make_unique<textlt::ActionEditorModal>(
        theme_, [this](const ActionItem& item, std::string& error) {
            std::string created_id;
            std::string created_page_id;
            const bool saved = item.id.empty()
                ? repository_.CreateItem(
                      selected_group_id_, SelectedPage() ? selected_page_id_ : std::string{},
                      item, created_id, error, &created_page_id)
                : repository_.UpdateItem(
                      selected_group_id_, selected_page_id_, item, error);
            if (!saved) return false;
            if (!created_page_id.empty()) {
                selected_page_id_ = std::move(created_page_id);
                navigator_tab_ = NavigatorTab::Pages;
                page_scroll_ = SelectedGroup()->pages.size() - 1;
                item_scroll_ = 0;
            }
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
    else if (!repository_.LastWarning().empty()) Notify(repository_.LastWarning());
    NormalizeSelection();
}

void ActionsWorkspaceComponent::Open() { context_menu_.Close(); NormalizeSelection(); TakeFocus(); }
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

bool ActionsWorkspaceComponent::UpdateProjectGroupName(const std::string& project_id,
    const std::string& project_name, std::string& error) {
    if (!repository_.UpdateProjectGroupName(project_id, project_name, error)) return false;
    if (project_ && project_->first == project_id) project_->second = project_name;
    return true;
}

bool ActionsWorkspaceComponent::MergeGroupsForNewProject(const std::string& project_id,
    const std::string& project_name, std::string& error) {
    error.clear();
    std::vector<std::string> sources;
    for (const auto& group : repository_.Data().groups) {
        const bool available = !group.project_id ||
            (is_missing_project_ && is_missing_project_(*group.project_id));
        if (!group.trash && available &&
            (group.name == project_name || BaseGroupName(group.name) == project_name)) {
            sources.push_back(group.id);
        }
    }
    if (sources.empty()) return true;

    std::string target;
    if (!repository_.EnsureProjectGroup(project_id, project_name, target, error)) return false;
    for (const auto& source : sources) {
        if (!repository_.MergeGroups(source, target, error)) return false;
        if (selected_group_id_ == source) selected_group_id_ = target;
    }
    return true;
}

bool ActionsWorkspaceComponent::DetachProjectGroup(
    const std::string& project_id, std::string& error) {
    return repository_.DetachProjectGroup(project_id, error);
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
    page_scroll_ = item_scroll_ = 0; NormalizeSelection(); focus_ = FocusArea::Navigator;
}

void ActionsWorkspaceComponent::Notify(const std::string& message) { if (status_) status_(message); }
void ActionsWorkspaceComponent::OpenNameDialog(Dialog dialog, std::string value) {
    dialog_ = dialog;
    name_input_ = std::move(value);
    *name_selection_ = {};
    name_selection_->cursor = static_cast<int>(name_input_.size());
    name_component_->TakeFocus();
}
void ActionsWorkspaceComponent::CloseDialog() {
    dialog_ = Dialog::None;
    rename_action_target_.reset();
    rename_action_error_.clear();
    TakeFocus();
}
void ActionsWorkspaceComponent::AfterMutation(const std::string& message) {
    NormalizeSelection(); if (groups_changed_) groups_changed_(); Notify(message); CloseDialog();
}

void ActionsWorkspaceComponent::NewGroup() {
    navigator_tab_ = NavigatorTab::Groups;
    focus_ = FocusArea::Navigator;
    OpenNameDialog(Dialog::NewGroup);
}
void ActionsWorkspaceComponent::RenameGroup() {
    navigator_tab_ = NavigatorTab::Groups;
    focus_ = FocusArea::Navigator;
    if (const auto* g = SelectedGroup()) OpenNameDialog(Dialog::RenameGroup, g->name);
}
void ActionsWorkspaceComponent::DeleteGroup() {
    navigator_tab_ = NavigatorTab::Groups;
    focus_ = FocusArea::Navigator;
    const ActionGroup* group = SelectedGroup();
    if (!group) return;
    if (!CanDeleteGroup(*group)) {
        Notify("This group belongs to a registered project.");
        return;
    }
    if (group->project_id) {
        std::string error;
        if (!repository_.ConvertGroupToGeneral(selected_group_id_, error)) {
            Notify("Group update failed: " + error);
            return;
        }
        if (groups_changed_) groups_changed_();
    }
    dialog_ = Dialog::DeleteGroup;
}

bool ActionsWorkspaceComponent::CanDeleteGroup(const ActionGroup& group) const {
    return !group.project_id ||
        (is_missing_project_ && is_missing_project_(*group.project_id));
}
void ActionsWorkspaceComponent::MergeGroup() {
    navigator_tab_ = NavigatorTab::Groups;
    focus_ = FocusArea::Navigator;
    const ActionGroup* selected = SelectedGroup();
    if (!selected) { Notify("No group selected."); return; }
    const auto& groups = repository_.Data().groups;
    const std::string base = BaseGroupName(selected->name);

    // Other groups sharing the same base name (e.g. "Foo" and "Foo (1)").
    std::vector<const ActionGroup*> candidates;
    for (const auto& g : groups) {
        if (g.id == selected->id) continue;
        if (BaseGroupName(g.name) == base) candidates.push_back(&g);
    }
    if (candidates.empty()) {
        Notify("No similar group to merge into.");
        return;
    }

    // The group to keep: prefer the one whose name matches the active project,
    // otherwise the non-numbered ("clean") name, otherwise the first candidate.
    const ActionGroup* target = nullptr;
    if (project_) {
        for (const auto& g : groups)
            if (BaseGroupName(g.name) == base && g.name == project_->second) {
                target = &g;
                break;
            }
    }
    if (!target) {
        for (const auto* c : candidates)
            if (!HasNumberedSuffix(c->name)) { target = c; break; }
    }
    if (!target) target = candidates.front();

    // The group merged away: the selected one, unless the selected one is the target.
    const ActionGroup* source = (selected->id == target->id) ? candidates.front() : selected;

    merge_target_id_ = target->id;
    dialog_ = Dialog::MergeGroup;
    name_component_->TakeFocus();
}
void ActionsWorkspaceComponent::NewPage() {
    navigator_tab_ = NavigatorTab::Pages;
    focus_ = FocusArea::Navigator;
    if (SelectedGroup()) OpenNameDialog(Dialog::NewPage);
}
void ActionsWorkspaceComponent::RenamePage() {
    navigator_tab_ = NavigatorTab::Pages;
    focus_ = FocusArea::Navigator;
    if (const auto* p = SelectedPage()) OpenNameDialog(Dialog::RenamePage, p->name);
}
void ActionsWorkspaceComponent::DeletePage() {
    navigator_tab_ = NavigatorTab::Pages;
    focus_ = FocusArea::Navigator;
    if (SelectedPage()) dialog_ = Dialog::DeletePage;
}
void ActionsWorkspaceComponent::AddAction() {
    if (!SelectedGroup()) {
        Notify("Select an Actions group before adding an action.");
        return;
    }
    action_editor_modal_->Open();
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
    std::string error;
    if (!repository_.MoveItem(selected_group_id_, selected_page_id_, selected_item_id_, delta, error)) {
        Notify("Action move failed: " + error);
        return;
    }
    if (const auto* page = SelectedPage()) {
        const int index = IndexById(page->items, selected_item_id_);
        if (index >= 0) {
            const size_t row = static_cast<size_t>(index);
            if (row < item_scroll_ || row >= item_scroll_ + items_visible_count_) {
                // Start at the moved item when the adjacent card is outside the
                // fully visible range. A partially visible card can be taller.
                item_scroll_ = row;
                items_visible_count_ = 1;
                item_boxes_.clear();
                item_box_ids_.clear();
            }
        }
    }
    AfterMutation("Action moved");
}
void ActionsWorkspaceComponent::CopySelectedAction(int tab_index) {
    const ActionItem* item = SelectedItem(); if (!item) { Notify("No action selected"); return; }
    if (item->tabs.empty()) { Notify("Action has no content"); return; }
    const size_t index = std::min(static_cast<size_t>(std::max(0, tab_index)),
        item->tabs.size() - 1);
    CopyActionBlock(*item, index);
}

bool ActionsWorkspaceComponent::CopyActionBlock(const ActionItem& item, size_t block) {
    const bool copied = block < item.tabs.size() && clipboard_ && clipboard_(item.tabs[block].content);
    Notify(copied ? "Copied action: " + item.title
        : "Could not copy action to the system clipboard");
    return copied;
}

void ActionsWorkspaceComponent::FlashCopyCaption(const std::string& item_id, size_t block) {
    copy_caption_deadlines_[{item_id, block}] =
        std::chrono::steady_clock::now() + kCopyCaptionFlashDuration;
}

void ActionsWorkspaceComponent::CopyAllActions() {
    const ActionPage* page = SelectedPage(); if (!page || page->items.empty()) { Notify("No actions to copy"); return; }
    std::string text;
    bool first = true;
    for (const ActionItem& item : page->items) {
        for (const ActionTab& tab : item.tabs) {
            if (!first) text += "\n\n";
            text += tab.content;
            first = false;
        }
    }
    Notify(clipboard_ && clipboard_(text) ? "Copied all actions" : "Could not copy actions to the system clipboard");
}

bool ActionsWorkspaceComponent::HasSelectedPage() const {
    return SelectedPage() != nullptr;
}

uint64_t ActionsWorkspaceComponent::Revision() const {
    const size_t group_hash = std::hash<std::string>{}(selected_group_id_);
    const size_t page_hash = std::hash<std::string>{}(selected_page_id_);
    const uint64_t scope = static_cast<uint64_t>(group_hash) ^
        (static_cast<uint64_t>(page_hash) + 0x9e3779b97f4a7c15ULL +
            (static_cast<uint64_t>(group_hash) << 6) +
            (static_cast<uint64_t>(group_hash) >> 2));
    return repository_.Revision() ^ scope;
}

std::vector<ActionsWorkspaceComponent::ActionSearchResult>
ActionsWorkspaceComponent::SearchActions(const std::string& query,
    bool match_case, bool whole_word, size_t limit, bool& truncated) {
    truncated = false;
    action_search_matches_.clear();
    action_search_active_ = 0;
    std::vector<ActionSearchResult> results;
    const ActionPage* page = SelectedPage();
    if (!page || query.empty()) return results;

    const std::string needle = match_case ? query : LowerCopy(query);
    auto scan = [&](const ActionItem& item, const std::string& text,
                    ActionField field, int tab_index,
                    const std::string& title_prefix) {
        if (text.empty()) return;
        const std::string searchable = match_case ? text : LowerCopy(text);
        size_t position = searchable.find(needle);
        while (position != std::string::npos) {
            const bool word_before = position > 0 &&
                IsWordCharacter(text[position - 1]);
            const size_t after = position + needle.size();
            const bool word_after = after < text.size() &&
                IsWordCharacter(text[after]);
            if (!whole_word || (!word_before && !word_after)) {
                ActionSearchResult match;
                match.item_id = item.id;
                match.field = field;
                match.tab_index = tab_index;
                match.byte = position;
                match.length = needle.size();
                match.label = title_prefix;
                if (field == ActionField::Content) {
                    const auto bounds = LineBounds(text, position);
                    match.preview = text.substr(
                        bounds.first, bounds.second - bounds.first);
                    match.preview_match_start = position - bounds.first;
                    match.preview_match_length = needle.size();
                    if (tab_index >= 0 &&
                        static_cast<size_t>(tab_index) < item.tabs.size() &&
                        item.tabs[static_cast<size_t>(tab_index)].hide_text) {
                        match.preview = kHiddenContentMask;
                        match.preview_match_start = 0;
                        match.preview_match_length = 0;
                    }
                } else {
                    match.preview = text;
                    match.preview_match_start = position;
                    match.preview_match_length = needle.size();
                }
                action_search_matches_.push_back(match);
                if (results.size() < limit) {
                    results.push_back(std::move(match));
                } else {
                    truncated = true;
                }
            }
            position = searchable.find(
                needle, position + std::max<size_t>(1, needle.size()));
        }
    };

    for (const ActionItem& item : page->items) {
        const std::string short_title = LimitUtf8Characters(item.title, 12);
        const std::string prefix = short_title.empty() ? "Action" : short_title;
        scan(item, item.title, ActionField::Title, 0, prefix + " · title");
        for (size_t tab_index = 0; tab_index < item.tabs.size(); ++tab_index) {
            const ActionTab& tab = item.tabs[tab_index];
            const std::string tab_label = tab_index == 0
                ? std::string{} : " · tab " + std::to_string(tab_index + 1);
            scan(item, tab.caption, ActionField::Caption,
                static_cast<int>(tab_index), prefix + tab_label + " · caption");
            scan(item, tab.content, ActionField::Content,
                static_cast<int>(tab_index), prefix + tab_label + " · content");
        }
    }
    return results;
}

void ActionsWorkspaceComponent::ActivateActionSearchResult(size_t index) {
    const ActionPage* page = SelectedPage();
    if (!page || index >= action_search_matches_.size()) return;
    const ActionSearchResult& match = action_search_matches_[index];
    const int item_index = IndexById(page->items, match.item_id);
    if (item_index < 0) return;
    action_search_active_ = index;
    selected_item_id_ = match.item_id;
    focus_ = FocusArea::Items;
    const size_t target = static_cast<size_t>(item_index);
    const size_t visible = items_visible_count_ ? items_visible_count_ : 1;
    if (target < item_scroll_) {
        item_scroll_ = target;
    } else if (target >= item_scroll_ + visible) {
        item_scroll_ = target + 1 - visible;
    }
}

bool ActionsWorkspaceComponent::ReplaceActionSearchResult(size_t index,
    const std::string& replacement, std::string& error) {
    if (index >= action_search_matches_.size()) {
        error = "No match to replace";
        return false;
    }
    const ActionSearchResult match = action_search_matches_[index];
    ActionTextEdit edit;
    edit.item_id = match.item_id;
    edit.tab_index = match.tab_index;
    edit.field = match.field;
    edit.byte = match.byte;
    edit.length = match.length;
    edit.replacement = replacement;
    std::vector<ActionTextEdit> edits = {std::move(edit)};
    if (!repository_.ApplyTextEdits(selected_group_id_, selected_page_id_,
            std::move(edits), error)) {
        return false;
    }
    NormalizeSelection();
    action_search_matches_.clear();
    action_search_active_ = 0;
    if (groups_changed_) groups_changed_();
    Notify("Replaced action text");
    return true;
}

bool ActionsWorkspaceComponent::ReplaceAllActionsText(
    const std::string& query, const std::string& replacement, bool match_case,
    bool whole_word, size_t& count, std::string& error) {
    count = 0;
    if (query.empty()) {
        error = "Enter text to replace";
        return false;
    }
    bool truncated = false;
    SearchActions(query, match_case, whole_word,
        static_cast<size_t>(-1), truncated);
    if (action_search_matches_.empty()) return true;
    std::vector<ActionTextEdit> edits;
    edits.reserve(action_search_matches_.size());
    for (const ActionSearchResult& match : action_search_matches_) {
        ActionTextEdit edit;
        edit.item_id = match.item_id;
        edit.tab_index = match.tab_index;
        edit.field = match.field;
        edit.byte = match.byte;
        edit.length = match.length;
        edit.replacement = replacement;
        edits.push_back(std::move(edit));
    }
    const size_t applied = edits.size();
    if (!repository_.ApplyTextEdits(selected_group_id_, selected_page_id_,
            std::move(edits), error)) {
        return false;
    }
    NormalizeSelection();
    action_search_matches_.clear();
    action_search_active_ = 0;
    count = applied;
    if (groups_changed_) groups_changed_();
    Notify("Replaced " + std::to_string(count) + " action matches");
    return true;
}

void ActionsWorkspaceComponent::ClearActionSearchHighlights() {
    action_search_matches_.clear();
    action_search_active_ = 0;
}

bool ActionsWorkspaceComponent::ImportActions(
    std::vector<ActionItem> items, std::string& error) {
    return ImportActionsTo(std::move(items), selected_group_id_, selected_page_id_, error);
}

bool ActionsWorkspaceComponent::ImportActionsTo(std::vector<ActionItem> items,
    const std::string& group_id, const std::string& page_id, std::string& error) {
    const size_t count = items.size();
    if (!repository_.AppendItems(group_id, page_id, std::move(items), error)) return false;
    selected_group_id_ = group_id;
    selected_page_id_ = page_id;
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
        case Dialog::RenameAction: {
            if (!rename_action_target_) return;
            const auto& target = *rename_action_target_;
            if (repository_.RenameItem(target.group_id, target.page_id, target.item_id, name_input_, rename_action_error_)) {
                AfterMutation("Action renamed");
            } else {
                Notify(rename_action_error_);
            }
            return;
        }
        case Dialog::DeleteGroup:
            if (repository_.DeleteGroup(selected_group_id_, error)) { selected_group_id_.clear(); AfterMutation("Group deleted"); } break;
        case Dialog::MergeGroup:
            if (repository_.MergeGroups(selected_group_id_, merge_target_id_, error)) {
                merge_target_id_.clear();
                AfterMutation("Groups merged");
            }
            break;
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

ftxui::Element ActionsWorkspaceComponent::RenderNavigator() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const bool show_groups = navigator_tab_ == NavigatorTab::Groups;

    if (pages_view_box_.x_max > pages_view_box_.x_min ||
        pages_view_box_.y_max > pages_view_box_.y_min) {
        pages_view_known_ = true;
    }
    if (pages_view_known_) {
        size_t visible_count = 0;
        const auto& boxes = show_groups ? group_boxes_ : page_boxes_;
        for (const auto& box : boxes) {
            if (box.y_max < pages_view_box_.y_min || box.y_min > pages_view_box_.y_max) break;
            ++visible_count;
        }
        if (visible_count > 0) {
            if (show_groups) groups_visible_count_ = visible_count;
            else pages_visible_count_ = visible_count;
        }
    }

    groups_tab_box_ = {};
    pages_tab_box_ = {};
    navigator_new_box_ = {};
    navigator_rename_box_ = {};
    navigator_delete_box_ = {};
    group_boxes_.clear();
    page_boxes_.clear();

    const ActionGroup* group = SelectedGroup();
    const ActionPage* page = SelectedPage();
    const bool can_modify_group = group && !group->project_id.has_value();
    const bool can_modify_page = page != nullptr;
    auto action_button = [&](const std::string& label, ButtonRole role,
                             bool enabled, Box& box) {
        ButtonSpec spec = ButtonSpecFromLabel(
            label, role, ButtonVariant::Minimal, ButtonSize::Compact);
        spec.enabled = enabled;
        return RenderModalFlatButton(theme, spec) | reflect(box);
    };

    Element tabs = hbox({
        RenderModalTabButton(theme, "Groups", show_groups) | reflect(groups_tab_box_),
        text(" "),
        RenderModalTabButton(theme, "Pages", !show_groups) | reflect(pages_tab_box_),
    });
    const bool can_create = show_groups || group != nullptr;
    const bool can_rename = show_groups ? can_modify_group : can_modify_page;
    const bool can_delete = show_groups ? group && CanDeleteGroup(*group) : can_modify_page;
    Element actions = hbox({
        action_button("New", ButtonRole::Primary, can_create, navigator_new_box_),
        action_button("Rename", ButtonRole::Utility, can_rename, navigator_rename_box_),
        action_button("Delete", ButtonRole::Danger, can_delete, navigator_delete_box_),
    });

    Elements rows;
    if (show_groups) {
        const auto& groups = repository_.Data().groups;
        const size_t visible_groups = groups_visible_count_ ? groups_visible_count_ : groups.size();
        const size_t max_scroll = groups.size() > visible_groups
            ? groups.size() - visible_groups : 0;
        if (group_scroll_ > max_scroll) group_scroll_ = max_scroll;
        if (groups.empty()) {
            rows.push_back(paragraph("No groups. Use New to create one.") |
                dim | color(theme.foreground));
        } else {
            const int selected = IndexById(groups, selected_group_id_);
            const size_t start = std::min(group_scroll_, groups.size() - 1);
            // reflect() retains Box references through layout, so reserve stable
            // storage before binding each visible row.
            group_boxes_.assign(groups.size() - start, {});
            for (size_t i = start; i < groups.size(); ++i) {
                const bool is_selected = static_cast<int>(i) == selected;
                std::string label = groups[i].project_id ? "◆ " : "  ";
                label += groups[i].name;
                Element row = paragraph(label) |
                    color(is_selected ? theme.modal_selected_item_fg : theme.foreground) |
                    reflect(group_boxes_[i - start]);
                if (is_selected) row = row | bgcolor(theme.modal_selected_item_bg);
                rows.push_back(row);
                rows.push_back(separator() | color(BorderColor(theme)));
            }
        }
    } else if (!group) {
        rows.push_back(paragraph("Select a group on the Groups tab.") |
            dim | color(theme.foreground));
    } else if (group->pages.empty()) {
        rows.push_back(paragraph("No pages. Use New to create one.") |
            dim | color(theme.foreground));
    } else {
        const size_t visible_pages = pages_visible_count_ ? pages_visible_count_ : group->pages.size();
        const size_t max_scroll = group->pages.size() > visible_pages
            ? group->pages.size() - visible_pages : 0;
        if (page_scroll_ > max_scroll) page_scroll_ = max_scroll;
        const int selected = IndexById(group->pages, selected_page_id_);
        const size_t start = std::min(page_scroll_, group->pages.size() - 1);
        page_boxes_.assign(group->pages.size() - start, {});
        for (size_t i = start; i < group->pages.size(); ++i) {
            const bool is_selected = static_cast<int>(i) == selected;
            Element row = paragraph("  " + group->pages[i].name) |
                color(is_selected ? theme.modal_selected_item_fg : theme.foreground) |
                reflect(page_boxes_[i - start]);
            if (is_selected) row = row | bgcolor(theme.modal_selected_item_bg);
            rows.push_back(row);
            rows.push_back(separator() | color(BorderColor(theme)));
        }
    }

    Elements content = {
        tabs,
        separator() | color(BorderColor(theme)),
        actions,
        separator() | color(BorderColor(theme)),
    };
    if (!show_groups) {
        content.push_back(
            text(" Group: " + (group ? group->name : std::string("None"))) |
            bold | color(theme.foreground));
        content.push_back(separator() | color(BorderColor(theme)));
    }
    content.push_back(vbox(std::move(rows)) | frame | yflex | reflect(pages_view_box_));
    return vbox(std::move(content)) |
        size(WIDTH, EQUAL, 32) |
        borderStyled(ftxui::LIGHT, BorderColor(theme)) |
        reflect(pages_box_);
}

ftxui::Element ActionsWorkspaceComponent::RenderItems() {
    using namespace ftxui; const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const auto now = std::chrono::steady_clock::now();
    for (auto entry = copy_caption_deadlines_.begin(); entry != copy_caption_deadlines_.end();) {
        if (entry->second <= now) entry = copy_caption_deadlines_.erase(entry);
        else ++entry;
    }
    if (!copy_caption_deadlines_.empty()) animation::RequestAnimationFrame();

    const ActionPage* page = SelectedPage();
    const bool page_changed = selected_page_id_ != items_layout_page_id_;
    if (page_changed) {
        items_layout_page_id_ = selected_page_id_;
        // Use a conservative first-frame estimate so a newly selected page is
        // scrollable before its variable-height cards have been laid out.
        items_visible_count_ = page && !page->items.empty() ? 1 : 0;
    }
    if (items_view_box_.x_max > items_view_box_.x_min ||
        items_view_box_.y_max > items_view_box_.y_min) {
        items_view_known_ = true;
    }
    size_t visible_rows = 0;
    if (items_view_known_) {
        visible_rows = static_cast<size_t>(
            std::max(0, items_view_box_.y_max - items_view_box_.y_min + 1));
        if (!page_changed) UpdateItemsVisibleCountFromLayout();
    }
    items_visible_rows_ = visible_rows;

    Elements cards; item_boxes_.clear(); item_box_ids_.clear(); copy_boxes_.clear();
    auto header_rows = std::make_shared<std::vector<int>>();
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
        item_box_ids_.reserve(item_boxes_.size());
        copy_boxes_.assign(page->items.size() - start, {});
        header_rows = std::make_shared<std::vector<int>>(
            page->items.size() - start, -1);
        for (size_t i = start; i < page->items.size(); ++i) {
            const auto& item = page->items[i];
            item_box_ids_.push_back(item.id);
            const bool is_selected = static_cast<int>(i) == selected;
            const std::string title = LimitActionMetadata(item.title);
            const ActionSearchResult* active_match =
                action_search_active_ < action_search_matches_.size() &&
                action_search_matches_[action_search_active_].item_id == item.id
                    ? &action_search_matches_[action_search_active_] : nullptr;
            Elements action_buttons;
            for (size_t tab_index = 0; tab_index < item.tabs.size() && tab_index < 4;
                 ++tab_index) {
                if (tab_index > 0 && item.tabs[tab_index].caption.empty() &&
                    item.tabs[tab_index].content.empty() &&
                    !item.tabs[tab_index].hide_text) continue;
                const std::string button_caption = item.tabs[tab_index].caption.empty()
                    ? (tab_index == 0 ? "Copy" : "Copy " + std::to_string(tab_index + 1))
                    : LimitActionCaption(item.tabs[tab_index].caption);
                const bool flashing = !copy_caption_deadlines_.empty() &&
                    copy_caption_deadlines_.count({item.id, tab_index}) != 0;
                ButtonSpec copy_spec = ButtonSpecFromLabel(
                    button_caption, flashing ? ButtonRole::Primary : ButtonRole::Secondary,
                    ButtonVariant::Minimal, ButtonSize::Compact);
                Element button = RenderButton(theme, copy_spec);
                if (active_match && active_match->field == ActionField::Caption &&
                    active_match->tab_index == static_cast<int>(tab_index)) {
                    const size_t begin = active_match->byte;
                    const size_t end = std::min(button_caption.size(),
                        begin + active_match->length);
                    if (begin < end) {
                        const ButtonStyle style = ResolveButtonStyle(
                            theme, copy_spec.role, ButtonState::Normal);
                        const auto match_flash_color = style.accent == theme.active_match_bg
                            ? theme.button_utility : style.accent;
                        Elements parts = {text(" ")};
                        if (begin > 0) parts.push_back(text(button_caption.substr(0, begin)) |
                            color(style.accent));
                        parts.push_back(text(button_caption.substr(begin, end - begin)) |
                            bgcolor(theme.active_match_bg) |
                            color(flashing ? match_flash_color : theme.selection_fg));
                        if (end < button_caption.size())
                            parts.push_back(text(button_caption.substr(end)) | color(style.accent));
                        parts.push_back(text(" "));
                        button = hbox(std::move(parts));
                    }
                }
                Element button_slot = hbox({filler(), std::move(button), filler()}) |
                    size(WIDTH, EQUAL, 14);
                action_buttons.push_back(std::move(button_slot) |
                    reflect(copy_boxes_[i - start][tab_index]) | notflex);
            }
            const size_t preview_tab = active_match &&
                active_match->field == ActionField::Content &&
                active_match->tab_index >= 0 &&
                static_cast<size_t>(active_match->tab_index) < item.tabs.size()
                    ? static_cast<size_t>(active_match->tab_index) : 0;
            const ActionTab empty_tab{};
            const ActionTab& tab = item.tabs.empty() ? empty_tab : item.tabs[preview_tab];
            const bool highlighted_content = active_match &&
                active_match->field == ActionField::Content &&
                static_cast<size_t>(std::max(0, active_match->tab_index)) == preview_tab;
            const std::string title_preview = LimitUtf8Characters(
                title, kActionMetadataFirstRowCharacters);
            Element title_row = hbox({
                    RenderHighlightedLine(theme, title_preview, 0,
                        active_match && active_match->field == ActionField::Title
                            ? active_match->byte : 0,
                        active_match && active_match->field == ActionField::Title
                            ? active_match->length : 0) | bold | xflex,
                    filler(),
                }) |
                bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg) |
                xflex;
            Element buttons_row = hbox({
                filler(), hbox(std::move(action_buttons)), text(" "),
            }) | xflex;
            Elements body = {std::move(title_row), std::move(buttons_row)};
            if (highlighted_content && !tab.hide_text) {
                const auto bounds = LineBounds(tab.content, active_match->byte);
                const std::string line = tab.content.substr(
                    bounds.first, bounds.second - bounds.first);
                body.push_back(hbox({
                    text("     "),
                    RenderHighlightedLine(theme, line, 0,
                        active_match->byte - bounds.first,
                        active_match->length) | xflex,
                }));
            }
            Element content = tab.hide_text
                ? hbox({text("     "), text(kHiddenContentMask) |
                    color(is_selected ? theme.modal_accent : theme.foreground)})
                : hbox({text("     "),
                    paragraph(tab.content) |
                        color(is_selected ? theme.modal_accent : theme.foreground) |
                        yframe |
                        size(HEIGHT, LESS_THAN, kActionContentPreviewRows) | xflex}) |
                    yframe |
                    size(HEIGHT, LESS_THAN, kActionContentPreviewRows);
            body.push_back(std::move(content));
            Element card_content = vbox(std::move(body)) | xflex;
            Element card = WithActionEndMarker(
                std::move(card_content), is_selected, theme.modal_accent,
                theme.modal_selected_item_bg, header_rows, i - start) |
                xflex | reflect(item_boxes_[i - start]);
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
        vbox(std::move(cards)) | yframe | xflex,
        ExtendHeaderBackground(RenderItemsScrollbar(
            visible_rows, total_items, visible_items, item_scroll_, max_scroll,
            theme, header_rows), header_rows, theme.modal_selected_item_bg),
    }) | yflex | reflect(items_view_box_);
    return vbox({title_row, separator() | color(BorderColor(theme)), list}) |
        borderStyled(ftxui::LIGHT, BorderColor(theme)) |
        reflect(items_box_) | xflex;
}

void ActionsWorkspaceComponent::UpdateItemsVisibleCountFromLayout() {
    if (!items_view_known_) return;
    size_t visible_count = 0;
    for (const auto& box : item_boxes_) {
        if (box.IsEmpty()) continue;
        if (box.y_max < items_view_box_.y_min) continue;
        if (box.y_min > items_view_box_.y_max) break;
        // reflect() clips a partially visible card to the viewport. A card
        // touching the lower boundary may therefore have additional rows
        // below the screen, so do not count it as fully visible.
        if (box.y_max >= items_view_box_.y_max) break;
        ++visible_count;
    }
    if (visible_count > 0) {
        items_visible_count_ = visible_count;
    }
}

ftxui::Element ActionsWorkspaceComponent::RenderItemsScrollbar(
    size_t visible_rows, size_t total_items, size_t visible_items, size_t scroll,
    size_t max_scroll, const Theme& theme,
    const std::shared_ptr<std::vector<int>>& header_rows) {
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
    else if (dialog_ == Dialog::MergeGroup) {
        const ActionGroup* source = SelectedGroup();
        const ActionGroup* target = nullptr;
        for (const auto& g : repository_.Data().groups)
            if (g.id == merge_target_id_) { target = &g; break; }
        body = vbox({
            text(" Merge '" + (source ? source->name : std::string{}) + "' into '" +
                (target ? target->name : std::string{}) + "'? ") |
                bold | color(theme.modal_text_color),
            text(" Pages with duplicate names are renumbered (1), (2), ... ") |
                color(theme.modal_text_color),
            text(" The source group is removed.") | color(theme.modal_text_color),
            text(" Enter: merge   Esc: cancel ") | dim | color(theme.modal_text_color),
        });
    }
    else if (dialog_ == Dialog::RenameAction) {
        Elements rows = {
            text(" Rename Action ") | bold | color(theme.modal_text_color),
            name_component_->Render(),
            text(" Enter: save   Esc: cancel ") | dim | color(theme.modal_text_color),
        };
        if (!rename_action_error_.empty()) {
            rows.push_back(paragraph(rename_action_error_) | color(theme.button_warning));
        }
        body = vbox(std::move(rows));
    }
    else body = vbox({text(" Name ") | bold | color(theme.modal_text_color), name_component_->Render(),
        text(" Enter: save   Esc: cancel ") | dim | color(theme.modal_text_color)});
    return body | size(WIDTH, GREATER_THAN, 48) | borderStyled(ftxui::LIGHT, BorderColor(theme)) | bgcolor(theme.menu_background) | clear_under | center;
}

ftxui::Element ActionsWorkspaceComponent::OnRender() {
    using namespace ftxui; Element base = hbox({RenderNavigator(), RenderItems()}) | flex;
    if (action_editor_modal_ && action_editor_modal_->IsOpen())
        return dbox({base, action_editor_modal_->View()->Render() | clear_under | center});
    if (dialog_ != Dialog::None) return dbox({base, RenderDialog()});
    return context_menu_.IsOpen() ? context_menu_.Overlay(std::move(base)) | flex : base;
}

ftxui::Component ActionsWorkspaceComponent::ActiveChild() {
    if (action_editor_modal_ && action_editor_modal_->IsOpen())
        return action_editor_modal_->View();
    if (dialog_ != Dialog::None) return name_component_;
    return nullptr;
}

ActionsContextMenu::Result ActionsWorkspaceComponent::ExecuteContextCommand(
    const ActionsContextMenu::Target& target, ActionsContextMenu::Command command, size_t block) {
    using Command = ActionsContextMenu::Command;
    using Result = ActionsContextMenu::Result;
    const auto& groups = repository_.Data().groups;
    const int group = IndexById(groups, target.group_id);
    if (group < 0) { Notify("Action is no longer available"); return Result::Close; }
    const auto& pages = groups[group].pages;
    const int page = IndexById(pages, target.page_id);
    if (page < 0) { Notify("Action is no longer available"); return Result::Close; }
    if (command == Command::Paste) {
        if (!read_clipboard_ || groups[group].trash) return Result::Close;
        read_clipboard_([this, target](std::optional<std::string> text) {
            if (!text || text->empty()) {
                Notify("Clipboard has no text");
                return;
            }
            auto action = DecodeActionSnapshot(*text);
            if (!action) {
                Notify("Clipboard does not contain a valid action item");
                return;
            }
            std::string id, error;
            if (!repository_.CreateItem(target.group_id, target.page_id, std::move(*action), id, error,
                    nullptr, target.item_id)) {
                Notify(error);
                return;
            }
            selected_group_id_ = target.group_id;
            selected_page_id_ = target.page_id;
            selected_item_id_ = std::move(id);
            focus_ = FocusArea::Items;
            const size_t row = static_cast<size_t>(IndexById(SelectedPage()->items, selected_item_id_));
            const size_t visible = std::max<size_t>(1, items_visible_count_);
            if (row < item_scroll_) item_scroll_ = row;
            else if (row - item_scroll_ >= visible) item_scroll_ = row + 1 - visible;
            if (groups_changed_) groups_changed_();
            Notify("Added a new action from the clipboard");
        });
        return Result::Close;
    }
    const auto& items = pages[page].items;
    const int index = IndexById(items, target.item_id);
    if (index < 0) { Notify("Action is no longer available"); return Result::Close; }
    const auto& item = items[index];
    if (command == Command::CopyBlock) {
        if (!CopyActionBlock(item, block)) return Result::ClipboardFailure;
        FlashCopyCaption(target.item_id, block);
        return Result::Close;
    }

    if (command == Command::CopyForConnect) {
        std::string snapshot;
        try {
            snapshot = EncodeActionSnapshot(item);
        } catch (const std::exception& error) {
            Notify(error.what());
            return Result::Close;
        }
        if (!clipboard_ || !clipboard_(snapshot)) {
            Notify("Could not copy action to the system clipboard");
            return Result::ClipboardFailure;
        }
        Notify("Copied action JSON for Connect");
        return Result::Close;
    }

    if (selected_page_id_ != target.page_id) item_scroll_ = static_cast<size_t>(index);
    selected_group_id_ = target.group_id;
    selected_page_id_ = target.page_id;
    selected_item_id_ = target.item_id;
    focus_ = FocusArea::Items;
    if (command == Command::Edit) {
        EditAction();
    } else if (command == Command::Rename) {
        rename_action_target_ = target;
        rename_action_error_.clear();
        OpenNameDialog(Dialog::RenameAction, item.title);
    } else if (command == Command::MoveUp && index > 0) {
        MoveAction(-1);
    } else if (command == Command::MoveDown && static_cast<size_t>(index) + 1 < items.size()) {
        MoveAction(1);
    }
    return Result::Close;
}

bool ActionsWorkspaceComponent::HandleContextMenuEvent(ftxui::Event event) {
    if (dialog_ != Dialog::None || (action_editor_modal_ && action_editor_modal_->IsOpen())) return false;
    if (context_menu_.IsOpen()) {
        return context_menu_.OnEvent(std::move(event),
            [this](const ActionsContextMenu::Target& target, ActionsContextMenu::Command command, size_t block) {
                return ExecuteContextCommand(target, command, block);
            });
    }
    if (!event.is_mouse()) return false;
    const auto& mouse = event.mouse();
    if (mouse.button != ftxui::Mouse::Right || mouse.motion != ftxui::Mouse::Pressed ||
        !items_view_box_.Contain(mouse.x, mouse.y) ||
        items_scrollbar_box_.Contain(mouse.x, mouse.y)) return false;
    const auto* page = SelectedPage();
    if (!page || page->id != items_layout_page_id_) return false;

    const auto* group = SelectedGroup();
    const bool can_paste = read_clipboard_ && group && !group->trash;
    if (page->items.empty() && can_paste) {
        focus_ = FocusArea::Items;
        TakeFocus();
        context_menu_.Open(mouse.x, mouse.y, {selected_group_id_, page->id, {}},
            nullptr, false, false, true);
        return true;
    }

    for (size_t row = 0; row < item_boxes_.size(); ++row) {
        if (!item_boxes_[row].Contain(mouse.x, mouse.y)) continue;
        const int index = IndexById(page->items, item_box_ids_[row]);
        if (index < 0) return false;
        const auto& item = page->items[index];
        selected_item_id_ = item.id;
        focus_ = FocusArea::Items;
        items_scrollbar_dragging_ = false;
        TakeFocus();
        context_menu_.Open(mouse.x, mouse.y, {selected_group_id_, page->id, item.id},
            &item, index > 0, static_cast<size_t>(index) + 1 < page->items.size(), can_paste);
        return true;
    }
    return false;
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
    if (HandleContextMenuEvent(event)) return true;
    const ActionGroup* group = SelectedGroup(); const ActionPage* page = SelectedPage();
    if (event == ftxui::Event::Tab) {
        focus_ = focus_ == FocusArea::Navigator ? FocusArea::Items : FocusArea::Navigator;
        return true;
    }
    if (focus_ == FocusArea::Navigator &&
        (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight)) {
        navigator_tab_ = event == ftxui::Event::ArrowLeft
            ? NavigatorTab::Groups : NavigatorTab::Pages;
        return true;
    }
    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::ArrowUp) {
        const int delta = event == ftxui::Event::ArrowDown ? 1 : -1;
        if (focus_ == FocusArea::Navigator && navigator_tab_ == NavigatorTab::Groups &&
            !repository_.Data().groups.empty()) {
            const auto& groups = repository_.Data().groups;
            int index = std::max(0, IndexById(groups, selected_group_id_));
            index = std::clamp(index + delta, 0, static_cast<int>(groups.size()) - 1);
            SelectGroup(groups[static_cast<size_t>(index)].id);
            const size_t selected_index = static_cast<size_t>(index);
            if (selected_index < group_scroll_) group_scroll_ = selected_index;
            else if (groups_visible_count_ > 0 &&
                     selected_index >= group_scroll_ + groups_visible_count_) {
                group_scroll_ = selected_index - groups_visible_count_ + 1;
            }
        } else if (focus_ == FocusArea::Navigator &&
                   navigator_tab_ == NavigatorTab::Pages && group && !group->pages.empty()) {
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
        UpdateItemsVisibleCountFromLayout();
        const size_t total_items = page ? page->items.size() : 0;
        const size_t visible_items = items_visible_count_ ? items_visible_count_ : total_items;
        const size_t max_scroll = total_items > visible_items ? total_items - visible_items : 0;
        if ((mouse.button == ftxui::Mouse::WheelUp ||
             mouse.button == ftxui::Mouse::WheelDown) &&
            pages_view_box_.Contain(mouse.x, mouse.y)) {
            const int delta = mouse.button == ftxui::Mouse::WheelUp
                ? -kMouseWheelScrollNavigator : kMouseWheelScrollNavigator;
            size_t* scroll = &page_scroll_;
            size_t total = group ? group->pages.size() : 0;
            size_t visible = pages_visible_count_ ? pages_visible_count_ : total;
            if (navigator_tab_ == NavigatorTab::Groups) {
                scroll = &group_scroll_;
                total = repository_.Data().groups.size();
                visible = groups_visible_count_ ? groups_visible_count_ : total;
            }
            const size_t navigator_max_scroll = total > visible ? total - visible : 0;
            *scroll = static_cast<size_t>(std::clamp<long long>(
                static_cast<long long>(*scroll) + delta, 0,
                static_cast<long long>(navigator_max_scroll)));
            return true;
        }
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
            if (groups_tab_box_.Contain(mouse.x, mouse.y)) {
                navigator_tab_ = NavigatorTab::Groups;
                focus_ = FocusArea::Navigator;
                return true;
            }
            if (pages_tab_box_.Contain(mouse.x, mouse.y)) {
                navigator_tab_ = NavigatorTab::Pages;
                focus_ = FocusArea::Navigator;
                return true;
            }
            if (navigator_new_box_.Contain(mouse.x, mouse.y)) {
                if (navigator_tab_ == NavigatorTab::Groups) NewGroup();
                else if (group) NewPage();
                return true;
            }
            if (navigator_rename_box_.Contain(mouse.x, mouse.y)) {
                if (navigator_tab_ == NavigatorTab::Groups) {
                    if (group && !group->project_id) RenameGroup();
                } else if (page) {
                    RenamePage();
                }
                return true;
            }
            if (navigator_delete_box_.Contain(mouse.x, mouse.y)) {
                if (navigator_tab_ == NavigatorTab::Groups) {
                    DeleteGroup();
                } else if (page) {
                    DeletePage();
                }
                return true;
            }
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
            const auto& groups = repository_.Data().groups;
            for (size_t i = 0; i < group_boxes_.size(); ++i) if (group_boxes_[i].Contain(mouse.x, mouse.y) && !groups.empty()) { SelectGroup(groups[std::min(group_scroll_, groups.size() - 1) + i].id); return true; }
            for (size_t i = 0; i < page_boxes_.size(); ++i) if (page_boxes_[i].Contain(mouse.x, mouse.y) && group && !group->pages.empty()) { selected_page_id_ = group->pages[std::min(page_scroll_, group->pages.size() - 1) + i].id; selected_item_id_.clear(); NormalizeSelection(); focus_ = FocusArea::Navigator; return true; }
            for (size_t i = 0; i < item_boxes_.size(); ++i) {
                if (!item_boxes_[i].Contain(mouse.x, mouse.y) || !page ||
                    page->items.empty()) continue;
                const ActionItem& clicked = page->items[
                    std::min(item_scroll_, page->items.size() - 1) + i];
                selected_item_id_ = clicked.id;
                focus_ = FocusArea::Items;
                for (size_t tab = 0; tab < copy_boxes_[i].size(); ++tab) {
                    if (copy_boxes_[i][tab].Contain(mouse.x, mouse.y)) {
                        CopySelectedAction(static_cast<int>(tab));
                        FlashCopyCaption(selected_item_id_, tab);
                        break;
                    }
                }
                return true;
            }
        }
    }
    return false;
}

} // namespace textlt::actions
