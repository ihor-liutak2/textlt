#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "actions/action_serializer.hpp"
#include "actions/action_snapshot.hpp"
#include "actions/actions_workspace_component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/terminal.hpp"
#include "modals/modal_action_editor.hpp"
#include "sidebar_component.hpp"

using namespace textlt;
using namespace textlt::actions;

namespace textlt {
std::filesystem::path GitManager::RepositoryRoot() { return {}; }
std::map<std::string, char> GitManager::GetFileStatuses() { return {}; }
} // namespace textlt

namespace textlt {
class ActionEditorModalTestAccess {
public:
    static void SetTab(ActionEditorModal& modal, size_t tab,
        const std::string& caption, const std::string& content, bool hide_text) {
        modal.content_->captions_[tab] = caption;
        modal.content_->contents_[tab] = content;
        modal.content_->hide_text_[tab] = hide_text;
    }
    static actions::ActionItem Draft(const ActionEditorModal& modal) {
        std::vector<actions::ActionTab> tabs;
        for (size_t index = 0; index < actions::kMaxActionTabs; ++index) {
            tabs.push_back({modal.content_->captions_[index],
                modal.content_->contents_[index],
                modal.content_->hide_text_[index]});
        }
        return actions::ActionItem::WithTabs(modal.content_->id_,
            modal.content_->title_, std::move(tabs));
    }
    static bool TitleFocused(const ActionEditorModal& modal) {
        return modal.content_->title_input_->Focused();
    }
    static void Render(ActionEditorModal& modal) {
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(48));
        ftxui::Render(screen, modal.content_->Render());
    }
    static std::string RenderText(ActionEditorModal& modal) {
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(48));
        ftxui::Render(screen, modal.content_->Render());
        return screen.ToString();
    }
    static std::string RenderModalText(ActionEditorModal& modal,
        int modal_height = 37) {
        modal.modal_->SetModalSize(102, modal_height);
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(48));
        ftxui::Render(screen, modal.View()->Render());
        return screen.ToString();
    }
    static int ExpectedPreferenceHeight(int desired_height) {
        const int terminal_height = ftxui::Terminal::Size().dimy;
        return terminal_height > 0
            ? std::min(desired_height, std::max(1, terminal_height - 2))
            : desired_height;
    }
    static bool ClickClear(ActionEditorModal& modal, const ftxui::Box& box) {
        ftxui::Mouse mouse;
        mouse.button = ftxui::Mouse::Left;
        mouse.motion = ftxui::Mouse::Pressed;
        mouse.x = (box.x_min + box.x_max) / 2;
        mouse.y = (box.y_min + box.y_max) / 2;
        return modal.OnEvent(ftxui::Event::Mouse("", mouse));
    }
    static bool ClearTitle(ActionEditorModal& modal) {
        return ClickClear(modal, modal.content_->title_clear_box_);
    }
    static bool ClearDescription(ActionEditorModal& modal) {
        return ClickClear(modal, modal.content_->caption_clear_boxes_[
            static_cast<size_t>(modal.content_->active_tab_)]);
    }
    static bool ClearContent(ActionEditorModal& modal) {
        return ClickClear(modal, modal.content_->content_clear_boxes_[
            static_cast<size_t>(modal.content_->active_tab_)]);
    }
    static void FocusCaption(ActionEditorModal& modal) {
        modal.content_->caption_inputs_[static_cast<size_t>(
            modal.content_->active_tab_)]->TakeFocus();
    }
    static void FocusContent(ActionEditorModal& modal) {
        modal.content_->content_inputs_[static_cast<size_t>(
            modal.content_->active_tab_)]->TakeFocus();
    }
    static void SelectTab(ActionEditorModal& modal, int tab) {
        modal.content_->SelectTab(tab);
    }
    static int ActiveTab(const ActionEditorModal& modal) {
        return modal.content_->active_tab_;
    }
    static ftxui::Box TabBox(const ActionEditorModal& modal, size_t tab) {
        return modal.content_->tab_boxes_[tab];
    }
    static ModalSizePreference SizePreference(const ActionEditorModal& modal) {
        return modal.content_->GetModalSizePreference();
    }
    static ftxui::Box ContentScrollbarBox(
        const ActionEditorModal& modal, size_t tab) {
        return modal.content_->content_scrollbar_boxes_[tab];
    }
    static bool ContentScrollbarImmediatelyFollowsInput(
        const ActionEditorModal& modal, size_t tab) {
        const auto& input = modal.content_->content_input_boxes_[tab];
        const auto& scrollbar = modal.content_->content_scrollbar_boxes_[tab];
        return !input.IsEmpty() && !scrollbar.IsEmpty() &&
            scrollbar.y_min == input.y_max + 1;
    }
    static int ContentCursor(const ActionEditorModal& modal, size_t tab) {
        return modal.content_->content_cursors_[tab];
    }
    static bool ContentFocused(const ActionEditorModal& modal) {
        return modal.content_->content_inputs_[static_cast<size_t>(
            modal.content_->active_tab_)]->Focused();
    }
    static bool Submit(ActionEditorModal& modal) {
        return modal.content_->Submit();
    }
};
} // namespace textlt

namespace textlt::actions {
class ActionsWorkspaceModalTestAccess {
public:
    static ActionItem Draft(const ActionsWorkspaceComponent& workspace) {
        return textlt::ActionEditorModalTestAccess::Draft(
            *workspace.action_editor_modal_);
    }
    static bool SaveNewAction(ActionsWorkspaceComponent& workspace, const std::string& title) {
        workspace.AddAction();
        if (!workspace.OnEvent(ftxui::Event::Character(title))) return false;
        auto& editor = *workspace.action_editor_modal_;
        if (!textlt::ActionEditorModalTestAccess::Submit(editor)) return false;
        editor.Close();
        return true;
    }
    static std::string RenderText(
        ActionsWorkspaceComponent& workspace, int width = 100, int height = 24) {
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
        ftxui::Render(screen, workspace.Render());
        return screen.ToString();
    }
    static bool NewPageDialogOpen(const ActionsWorkspaceComponent& workspace) {
        return workspace.dialog_ == ActionsWorkspaceComponent::Dialog::NewPage;
    }
    static bool NewGroupDialogOpen(const ActionsWorkspaceComponent& workspace) {
        return workspace.dialog_ == ActionsWorkspaceComponent::Dialog::NewGroup;
    }
    static bool DeleteGroupDialogOpen(const ActionsWorkspaceComponent& workspace) {
        return workspace.dialog_ == ActionsWorkspaceComponent::Dialog::DeleteGroup;
    }
    static bool MergeGroupDialogOpen(const ActionsWorkspaceComponent& workspace) {
        return workspace.dialog_ == ActionsWorkspaceComponent::Dialog::MergeGroup;
    }
    static bool DialogIsNone(const ActionsWorkspaceComponent& workspace) {
        return workspace.dialog_ == ActionsWorkspaceComponent::Dialog::None;
    }
    static std::string MergeTargetId(const ActionsWorkspaceComponent& workspace) {
        return workspace.merge_target_id_;
    }
    static int FirstItemHeight(const ActionsWorkspaceComponent& workspace) {
        if (workspace.item_boxes_.empty()) return 0;
        const ftxui::Box& box = workspace.item_boxes_.front();
        return box.y_max - box.y_min + 1;
    }
    static bool FirstItemFillsCardWidth(
        const ActionsWorkspaceComponent& workspace) {
        if (workspace.item_boxes_.empty()) return false;
        const ftxui::Box& item = workspace.item_boxes_.front();
        return item.x_max >= workspace.items_view_box_.x_max - 3;
    }
    static bool FirstActionMarkerAndHeaderStyleAreCorrect(
        ActionsWorkspaceComponent& workspace) {
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(24));
        ftxui::Render(screen, workspace.Render());
        if (workspace.item_boxes_.empty()) return false;
        const ftxui::Box& item = workspace.item_boxes_.front();
        const Theme& theme = FallbackTheme();
        if (screen.CellAt(item.x_min + 1, item.y_min).background_color !=
            theme.modal_selected_item_bg) return false;
        if (screen.CellAt(item.x_min + 1, item.y_min + 1).background_color ==
            theme.modal_selected_item_bg) return false;
        const ftxui::Box& scrollbar = workspace.items_scrollbar_box_;
        if (!scrollbar.Contain(scrollbar.x_min, item.y_min) ||
            screen.CellAt(scrollbar.x_min, item.y_min).background_color !=
                theme.modal_selected_item_bg) return false;
        for (int y = item.y_min; y <= item.y_max; ++y) {
            const ftxui::Cell& marker = screen.CellAt(item.x_min, y);
            if (marker.character != "│" ||
                marker.foreground_color != theme.button_success) return false;
            if (y == item.y_min &&
                marker.background_color != theme.modal_selected_item_bg) return false;
        }
        return true;
    }
    static bool FirstCopyButtonVisible(const ActionsWorkspaceComponent& workspace) {
        if (workspace.copy_boxes_.empty()) return false;
        const ftxui::Box& box = workspace.copy_boxes_.front()[0];
        return workspace.items_box_.Contain(box.x_min, box.y_min) &&
            workspace.items_box_.Contain(box.x_max, box.y_max);
    }
    static size_t FirstActionCopyButtonCount(
        const ActionsWorkspaceComponent& workspace) {
        if (workspace.copy_boxes_.empty()) return 0;
        size_t count = 0;
        for (const ftxui::Box& box : workspace.copy_boxes_.front())
            if (!box.IsEmpty()) ++count;
        return count;
    }
    static bool FirstActionCopyButtonsEqualWidth(
        const ActionsWorkspaceComponent& workspace) {
        if (workspace.copy_boxes_.empty()) return false;
        const auto& boxes = workspace.copy_boxes_.front();
        const int width = boxes[0].x_max - boxes[0].x_min + 1;
        for (const ftxui::Box& box : boxes) {
            if (box.IsEmpty() || box.x_max - box.x_min + 1 != width) return false;
        }
        return true;
    }
    static bool FirstActionCopyButtonsDoNotOverlap(
        const ActionsWorkspaceComponent& workspace) {
        if (workspace.copy_boxes_.empty()) return false;
        const auto& boxes = workspace.copy_boxes_.front();
        for (size_t left = 0; left < boxes.size(); ++left) {
            if (boxes[left].IsEmpty()) continue;
            for (size_t right = left + 1; right < boxes.size(); ++right) {
                if (boxes[right].IsEmpty()) continue;
                if (ftxui::Box::Intersection(boxes[left], boxes[right]).IsEmpty())
                    continue;
                return false;
            }
        }
        return true;
    }
    static bool FirstActionCopyButtonsShareOneRow(
        const ActionsWorkspaceComponent& workspace) {
        if (workspace.copy_boxes_.empty()) return false;
        const auto& boxes = workspace.copy_boxes_.front();
        if (workspace.item_boxes_.empty() || boxes[0].IsEmpty()) return false;
        const int button_y = boxes[0].y_min;
        if (button_y != workspace.item_boxes_.front().y_min + 2) return false;
        for (const ftxui::Box& box : boxes) {
            if (box.IsEmpty()) continue;
            if (box.y_min != button_y || box.y_max != button_y) return false;
        }
        return true;
    }
    static bool ClickFirstActionCopyButton(
        ActionsWorkspaceComponent& workspace, size_t tab_index) {
        if (workspace.copy_boxes_.empty() ||
            tab_index >= actions::kMaxActionTabs) return false;
        const ftxui::Box box = workspace.copy_boxes_.front()[tab_index];
        if (box.IsEmpty()) return false;
        ftxui::Mouse mouse;
        mouse.button = ftxui::Mouse::Left;
        mouse.motion = ftxui::Mouse::Pressed;
        mouse.x = (box.x_min + box.x_max) / 2;
        mouse.y = (box.y_min + box.y_max) / 2;
        return workspace.OnEvent(ftxui::Event::Mouse("", mouse));
    }
    static size_t ItemScroll(const ActionsWorkspaceComponent& workspace) {
        return workspace.item_scroll_;
    }
    static bool WheelItems(
        ActionsWorkspaceComponent& workspace, ftxui::Mouse::Button button) {
        ftxui::Mouse mouse;
        mouse.button = button;
        mouse.motion = ftxui::Mouse::Pressed;
        mouse.x = (workspace.items_view_box_.x_min + workspace.items_view_box_.x_max) / 2;
        mouse.y = (workspace.items_view_box_.y_min + workspace.items_view_box_.y_max) / 2;
        return workspace.OnEvent(ftxui::Event::Mouse("", mouse));
    }
    static bool ItemsScrollbarVisible(const ActionsWorkspaceComponent& workspace) {
        const ftxui::Box& box = workspace.items_scrollbar_box_;
        return box.x_min <= box.x_max && box.y_min <= box.y_max &&
            workspace.items_box_.Contain(box.x_min, box.y_min) &&
            workspace.items_box_.Contain(box.x_max, box.y_max);
    }
    static bool ItemsNeedScrolling(const ActionsWorkspaceComponent& workspace) {
        const ActionPage* page = workspace.SelectedPage();
        return page && workspace.items_visible_count_ > 0 &&
            workspace.items_visible_count_ < page->items.size();
    }
    static bool HasCopyCaptionFlash(
        const ActionsWorkspaceComponent& workspace, const std::string& id, size_t block) {
        return workspace.copy_caption_deadlines_.count({id, block}) != 0;
    }
    static void ClearCopyCaptionFlashes(ActionsWorkspaceComponent& workspace) {
        workspace.copy_caption_deadlines_.clear();
    }
    static ftxui::Box ActionBox(const ActionsWorkspaceComponent& workspace, const std::string& id) {
        for (size_t i = 0; i < workspace.item_box_ids_.size(); ++i) {
            if (workspace.item_box_ids_[i] == id) {
                return ftxui::Box::Intersection(workspace.item_boxes_[i], workspace.items_view_box_);
            }
        }
        return {0, -1, 0, -1};
    }
    static std::string SelectedItemId(const ActionsWorkspaceComponent& workspace) {
        return workspace.selected_item_id_;
    }
    static void SelectItem(ActionsWorkspaceComponent& workspace, const std::string& id) {
        workspace.selected_item_id_ = id;
    }
    static ftxui::Box SelectPageAndRenderItems(ActionsWorkspaceComponent& workspace, const std::string& id) {
        workspace.selected_page_id_ = id;
        RenderText(workspace);
        return workspace.items_view_box_;
    }
    static ftxui::Box AddActionBox(const ActionsWorkspaceComponent& workspace) {
        return workspace.add_action_box_;
    }
    static ActionEditorModal& ActionEditor(ActionsWorkspaceComponent& workspace) {
        return *workspace.action_editor_modal_;
    }
};
} // namespace textlt::actions

#define Check(condition)                                                        \
    do {                                                                        \
        std::fprintf(stderr, "Line %d: %s\n", __LINE__, #condition);            \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "Check failed at %s:%d: %s\n",               \
                __FILE__, __LINE__, #condition);                                \
            std::abort();                                                       \
        }                                                                       \
    } while (false)

namespace {

int FindRow(const ftxui::Screen& screen, const std::string& label) {
    for (int y = 0; y < screen.dimy(); ++y) {
        std::string row;
        for (int x = 0; x < screen.dimx(); ++x)
            row += screen.PixelAt(x, y).character;
        if (row.find(label) != std::string::npos) return y;
    }
    return -1;
}

void TestHierarchicalActionsSidebar() {
    std::string selected_group;
    std::string selected_page;
    std::optional<SidebarPanel::ActionCommand> context_command;
    SidebarPanel sidebar(
        [](const std::filesystem::path&) {}, nullptr, nullptr,
        [] { return std::vector<std::filesystem::path>{}; },
        [](const std::filesystem::path&) {}, [] {}, [] {},
        [](const std::filesystem::path&) {},
        [](const std::string&) { return std::string{}; },
        [&](const std::string& group, const std::string& page) {
            selected_group = group;
            selected_page = page;
        },
        [&](SidebarPanel::ActionCommand command) { context_command = command; },
        [] { return std::vector<SidebarPanel::ProjectEntry>{}; },
        [](const std::string&) {}, [](const std::filesystem::path&) {},
        [](const std::string&) {}, [](const std::string&) {},
        [](const std::string&) {}, [](const std::string&) {},
        [](const std::string&) {}, [](const std::string&) {});
    std::vector<SidebarPanel::ActionGroupEntry> groups = {
        {"g-textlt", "textlt", {{"p1", "Page1"}, {"pi", "install"}}},
        {"g-ihor", "ihorlt", {{"pa", "PageA"}, {"pb", "PageB"}}},
    };
    sidebar.SetActionGroups(groups, "g-textlt", "p1");
    sidebar.ShowActions();
    auto screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(36), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, sidebar.Render());
    const int group_row = FindRow(screen, "textlt");
    const int groups_pages_title = FindRow(screen, "Groups / Pages");
    const int page_row = FindRow(screen, "    Page1");
    const int second_page_row = FindRow(screen, "    PageB");
    Check(group_row >= 0 && groups_pages_title >= 0 && page_row >= 0 &&
        second_page_row >= 0);
    Check(group_row == groups_pages_title + 3);
    Check(FindRow(screen, "Move Up") < 0 && FindRow(screen, "New") < 0);
    Check(group_row < page_row && page_row < second_page_row);
    Check(screen.PixelAt(0, group_row).character == "t");
    Check(screen.PixelAt(4, page_row).character == "P");
    const Theme& theme = FallbackTheme();
    Check(screen.CellAt(0, group_row).background_color !=
        theme.modal_selected_item_bg);
    Check(screen.CellAt(0, group_row).foreground_color == theme.modal_accent);
    Check(screen.CellAt(4, page_row).background_color ==
        theme.modal_selected_item_bg);
    bool no_decorative_separators = true;
    for (int y = 0; y < screen.dimy(); ++y) {
        for (int x = 0; x < screen.dimx(); ++x) {
            const std::string& cell = screen.PixelAt(x, y).character;
            no_decorative_separators &= cell != "┌" && cell != "┐" &&
                cell != "└" && cell != "┘" && cell != "│" && cell != "─";
        }
    }
    Check(no_decorative_separators);
    ftxui::Mouse mouse;
    mouse.button = ftxui::Mouse::Left;
    mouse.motion = ftxui::Mouse::Pressed;
    mouse.x = 0;
    mouse.y = group_row;
    Check(sidebar.OnEvent(ftxui::Event::Mouse("", mouse)));
    Check(selected_group == "g-textlt" && selected_page.empty());
    mouse.x = 5;
    mouse.y = second_page_row;
    Check(sidebar.OnEvent(ftxui::Event::Mouse("", mouse)));
    Check(selected_group == "g-ihor" && selected_page == "pb");
    Check(sidebar.OnEvent(ftxui::Event::ArrowUp));
    Check(selected_group == "g-ihor" && selected_page == "pa");
    mouse.x = 0;
    mouse.y = group_row;
    mouse.button = ftxui::Mouse::Right;
    Check(sidebar.OnEvent(ftxui::Event::Mouse("", mouse)));
    Check(selected_group == "g-textlt" && selected_page.empty());
    auto popup = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(36), ftxui::Dimension::Fixed(24));
    ftxui::Render(popup, sidebar.Render());
    const int new_group_row = FindRow(popup, "New");
    Check(new_group_row >= 0);
    Check(new_group_row == group_row + 1);
    mouse.button = ftxui::Mouse::Left;
    mouse.x = 5;
    mouse.y = new_group_row;
    Check(sidebar.OnEvent(ftxui::Event::Mouse("", mouse)));
    Check(context_command == SidebarPanel::ActionCommand::NewGroup);
    mouse.button = ftxui::Mouse::Right;
    mouse.x = 0;
    mouse.y = group_row;
    Check(sidebar.OnEvent(ftxui::Event::Mouse("", mouse)));
    Check(sidebar.OnEvent(ftxui::Event::ArrowDown));
    Check(sidebar.OnEvent(ftxui::Event::Return));
    Check(context_command == SidebarPanel::ActionCommand::RenameGroup);
    mouse.button = ftxui::Mouse::Right;
    Check(sidebar.OnEvent(ftxui::Event::Mouse("", mouse)));
    Check(sidebar.OnEvent(ftxui::Event::ArrowDown));
    Check(sidebar.OnEvent(ftxui::Event::ArrowDown));
    Check(sidebar.OnEvent(ftxui::Event::Return));
    Check(context_command == SidebarPanel::ActionCommand::DeleteGroup);

    mouse.button = ftxui::Mouse::Right;
    mouse.x = 5;
    mouse.y = second_page_row;
    Check(sidebar.OnEvent(ftxui::Event::Mouse("", mouse)));
    Check(selected_group == "g-ihor" && selected_page == "pb");
    popup = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(36), ftxui::Dimension::Fixed(24));
    ftxui::Render(popup, sidebar.Render());
    const int new_page_row = FindRow(popup, "New");
    Check(new_page_row >= 0);
    Check(new_page_row == second_page_row + 1);
    mouse.button = ftxui::Mouse::Left;
    mouse.x = 10;
    mouse.y = new_page_row;
    Check(sidebar.OnEvent(ftxui::Event::Mouse("", mouse)));
    Check(context_command == SidebarPanel::ActionCommand::NewPage);
}

ftxui::Box FindActionsMenu(const ftxui::Screen& screen) {
    for (int y = 1; y < screen.dimy() - 1; ++y) {
        for (int x = 0; x + 13 < screen.dimx(); ++x) {
            if (screen.PixelAt(x, y).character != "│") continue;
            std::string label;
            for (int i = 1; i <= 12; ++i) label += screen.PixelAt(x + i, y).character;
            if (label != " Edit Action") continue;
            int top = y;
            while (top > 0 && screen.PixelAt(x, top).character != "┌") --top;
            int bottom = y;
            while (bottom < screen.dimy() - 1 && screen.PixelAt(x, bottom).character != "└") ++bottom;
            int right = x + 1;
            while (right < screen.dimx() - 1 && screen.PixelAt(right, top).character != "┐") ++right;
            Check(screen.PixelAt(x, top).character == "┌");
            Check(screen.PixelAt(right, bottom).character == "┘");
            return {x, right, top, bottom};
        }
    }
    Check(false);
    return {};
}

void TestActionsContextMenu(const std::filesystem::path& path) {
    using Access = ActionsWorkspaceModalTestAccess;
    using ftxui::Event;
    using ftxui::Mouse;
    using EditorAccess = ActionEditorModalTestAccess;
    ActionsDocument document;
    ActionPage page{"context-page", "Context page", {}};
    for (size_t count = 1; count <= kMaxActionTabs; ++count) {
        std::vector<ActionTab> blocks;
        for (size_t block = 0; block < count; ++block) {
            const std::string suffix = std::to_string(count) + "-" + std::to_string(block);
            blocks.push_back({count == 4 && block == 3 ? "" : "Caption " + suffix,
                "\t  Текст " + suffix + "\r\n配置\n  ", true});
        }
        page.items.push_back(ActionItem::WithTabs("action-" + std::to_string(count),
            "Target " + std::to_string(count), std::move(blocks)));
    }
    document.groups.push_back({"context-group", "Context group", std::nullopt, false,
        {page, {"empty-page", "Empty destination", {}}}});
    std::string error;
    Check(ActionSerializer::Save(path, document, error));

    std::string status;
    std::vector<std::string> copies;
    bool clipboard_available = true;
    ActionsWorkspaceComponent workspace(nullptr, [&](const std::string& value) { status = value; },
        [&](const std::string& value) { copies.push_back(value); return clipboard_available; }, [] {}, path);
    workspace.SelectPage("context-group", "context-page");
    std::optional<std::string> clipboard_text;
    workspace.SetClipboardReader([&](ClipboardController::PasteCallback deliver) {
        deliver(clipboard_text);
    });
    const auto draw = [&](int width = 105, int height = 180) {
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
        ftxui::Render(screen, workspace.Render());
        return screen;
    };
    const auto mouse = [&](Mouse::Button button, Mouse::Motion motion, int x, int y) {
        Mouse value{};
        value.button = button;
        value.motion = motion;
        value.x = x;
        value.y = y;
        return workspace.OnEvent(Event::Mouse("", value));
    };
    const auto open = [&](const std::string& id, bool edge = false) {
        const auto card = Access::ActionBox(workspace, id);
        Check(!card.IsEmpty());
        Check(mouse(Mouse::Right, Mouse::Pressed,
            edge ? card.x_max : card.x_min + 1, edge ? card.y_max : card.y_min));
        Check(workspace.HasOpenContextMenu());
        Check(Access::SelectedItemId(workspace) == id);
    };
    const auto click_row = [&](const ftxui::Box& menu, size_t row) {
        const int y = menu.y_min + 1 + static_cast<int>(row);
        Check(mouse(Mouse::Left, Mouse::Pressed, menu.x_min + 2, y));
        Check(mouse(Mouse::Left, Mouse::Released, menu.x_min + 2, y));
    };
    const auto open_menu = [&](const std::string& id) {
        open(id);
        return FindActionsMenu(draw());
    };

    draw();
    const auto add = Access::AddActionBox(workspace);
    Check(!mouse(Mouse::Right, Mouse::Pressed, add.x_min + 1, add.y_min));
    Check(!workspace.HasOpenContextMenu());

    for (const auto& action : page.items) {
        for (size_t block = 0; block < action.tabs.size(); ++block) {
            Access::ClearCopyCaptionFlashes(workspace);
            Access::SelectItem(workspace, "action-1");
            open(action.id, true);
            const auto screen = draw();
            const auto menu = FindActionsMenu(screen);
            Check(menu.y_max - menu.y_min + 1 == static_cast<int>(action.tabs.size()) + 9);
            Check(menu.x_min >= 0 && menu.y_min >= 0 && menu.x_max < screen.dimx() && menu.y_max < screen.dimy());
            Check(screen.PixelAt(menu.x_min + 1, menu.y_min + 1 + static_cast<int>(action.tabs.size())).character == "─");
            for (size_t row = 0; row < action.tabs.size(); ++row) {
                std::string label;
                for (int x = menu.x_min + 2; x < menu.x_max; ++x) {
                    label += screen.PixelAt(x, menu.y_min + 1 + static_cast<int>(row)).character;
                }
                label.erase(label.find_last_not_of(' ') + 1);
                Check(label == action.tabs[row].caption);
            }
            // A different selection after opening cannot retarget the copy.
            Access::SelectItem(workspace, action.id == "action-1" ? "action-2" : "action-1");
            const size_t old_copies = copies.size();
            if (block % 2 == 0) {
                for (size_t index = 0; index < block; ++index) Check(workspace.OnEvent(Event::ArrowDown));
                Check(workspace.OnEvent(Event::Return));
            } else {
                click_row(menu, block);
            }
            Check(!workspace.HasOpenContextMenu() && copies.size() == old_copies + 1);
            Check(copies.back() == action.tabs[block].content);
            Check(Access::HasCopyCaptionFlash(workspace, action.id, block));
            Check(!Access::HasCopyCaptionFlash(
                workspace, action.id, (block + 1) % kMaxActionTabs));
        }
    }

    Access::ClearCopyCaptionFlashes(workspace);
    auto menu = open_menu("action-2");
    Check(mouse(Mouse::None, Mouse::Moved, menu.x_min + 3, menu.y_min + 2));
    clipboard_available = false;
    Check(workspace.OnEvent(Event::Return) && workspace.HasOpenContextMenu());
    Check(copies.back() == page.items[1].tabs[1].content);
    Check(status == "Could not copy action to the system clipboard");
    Check(!Access::HasCopyCaptionFlash(workspace, "action-2", 1));
    Check(draw().ToString().find("Clipboard write failed") != std::string::npos);
    clipboard_available = true;
    Check(workspace.OnEvent(Event::Return) && !workspace.HasOpenContextMenu());
    Check(Access::HasCopyCaptionFlash(workspace, "action-2", 1));

    open_menu("action-1");
    Check(mouse(Mouse::Left, Mouse::Pressed, add.x_min + 1, add.y_min));
    Check(!workspace.HasOpenContextMenu() && !Access::ActionEditor(workspace).IsOpen());
    open_menu("action-1");
    Check(workspace.OnEvent(Event::Escape) && !workspace.HasOpenContextMenu());
    menu = open_menu("action-1");
    click_row(menu, 4);  // Move Up.
    Check(workspace.HasOpenContextMenu());
    Check(workspace.OnEvent(Event::Escape));
    menu = open_menu("action-8");
    click_row(menu, 12);  // Disabled Move Down on the last action.
    Check(workspace.HasOpenContextMenu());
    Check(workspace.OnEvent(Event::Escape));

    menu = open_menu("action-3");
    click_row(menu, 5);  // Rename Action.
    Check(!workspace.HasOpenContextMenu());
    Check(draw().ToString().find("Rename Action") != std::string::npos);
    Check(workspace.OnEvent(Event::Character(" renamed")));
    Check(workspace.OnEvent(Event::Return));
    Check(workspace.Groups()[0].pages[0].items[2].title == "Target 3 renamed");
    Check(workspace.Groups()[0].pages[0].items[2].tabs[1].content == page.items[2].tabs[1].content);

    menu = open_menu("action-2");
    click_row(menu, 3);  // Edit Action.
    auto& editor = Access::ActionEditor(workspace);
    Check(editor.IsOpen() && Access::Draft(workspace).id == "action-2");
    EditorAccess::FocusContent(editor);
    Check(workspace.OnEvent(Event::Character("Edited")));
    Check(EditorAccess::Submit(editor));
    editor.Close();

    draw(54, 10);
    draw(54, 10);
    open("action-1", true);
    auto small = draw(45, 10);
    Check(workspace.HasOpenContextMenu());
    const auto narrow_menu = FindActionsMenu(small);
    Check(narrow_menu.x_min >= 0 && narrow_menu.x_max < small.dimx());
    for (int i = 0; i < 3; ++i) Check(workspace.OnEvent(Event::ArrowUp));
    small = draw(45, 10);
    Check(small.ToString().find("Move Down") != std::string::npos);
    Check(workspace.OnEvent(Event::Return) && !workspace.HasOpenContextMenu());
    Check(workspace.Groups()[0].pages[0].items[1].id == "action-1");
    draw(54, 10);
    Check(Access::SelectedItemId(workspace) == "action-1");
    Check(!Access::ActionBox(workspace, "action-1").IsEmpty());
    draw();
    menu = open_menu("action-1");
    click_row(menu, 4);  // Move Up.
    Check(workspace.Groups()[0].pages[0].items[0].id == "action-1");

    ActionsDocument saved;
    Check(ActionSerializer::Load(path, saved, error));
    const auto& items = saved.groups[0].pages[0].items;
    Check(items[0].id == "action-1" && items[1].id == "action-2");
    Check(items[1].tabs[0].content.find("Edited") != std::string::npos);
    Check(items[2].title == "Target 3 renamed");
    Check(items[3].tabs[3].content == page.items[3].tabs[3].content);

    draw();
    draw();
    menu = open_menu("action-4");
    click_row(menu, 9);  // Copy for Connect.
    Check(status == "Copied action JSON for Connect");
    const auto snapshot = copies.back();
    const auto decoded = DecodeActionSnapshot(snapshot);
    Check(decoded && decoded->id.empty() && decoded->title == page.items[3].title);
    Check(decoded->tabs.size() == 4);
    for (size_t i = 0; i < decoded->tabs.size(); ++i) {
        Check(decoded->tabs[i].caption == page.items[3].tabs[i].caption);
        Check(decoded->tabs[i].content == page.items[3].tabs[i].content);
        Check(decoded->tabs[i].hide_text == page.items[3].tabs[i].hide_text);
    }
    menu = open_menu("action-8");
    click_row(menu, 13);  // Copy all eight tabs for Connect.
    Check(status == "Copied action JSON for Connect");
    const std::string eight_tab_snapshot = copies.back();
    const auto decoded_eight_tab = DecodeActionSnapshot(eight_tab_snapshot);
    Check(decoded_eight_tab && decoded_eight_tab->tabs.size() == kMaxActionTabs);
    for (size_t index = 0; index < kMaxActionTabs; ++index) {
        Check(decoded_eight_tab->tabs[index].caption == page.items[7].tabs[index].caption);
        Check(decoded_eight_tab->tabs[index].content == page.items[7].tabs[index].content);
        Check(decoded_eight_tab->tabs[index].hide_text ==
            page.items[7].tabs[index].hide_text);
    }
    for (const auto& invalid : {std::string("not JSON"),
            std::string(R"({"title":"Missing tabs","tabs":[]})"),
            std::string(R"({"title":"Wrong flag","tabs":[{"caption":"Run","content":"text","hide_text":"true"}]})"),
            snapshot + '\0' + "ignored"}) {
        clipboard_text = invalid;
        menu = open_menu("action-1");
        click_row(menu, 7);  // Paste.
        Check(status == "Clipboard does not contain a valid action item");
            Check(workspace.Groups()[0].pages[0].items.size() == 8);
    }

    clipboard_text = snapshot;
    menu = open_menu("action-1");
    click_row(menu, 7);  // Paste.
    const auto with_copy = workspace.Groups()[0].pages[0].items;
    Check(with_copy.size() == 9 && with_copy[1].title == decoded->title);
    Check(!with_copy[1].id.empty() && Access::SelectedItemId(workspace) == with_copy[1].id);
    Check(EncodeActionSnapshot(with_copy[1]) == snapshot);
    for (size_t index = 0; index < items.size(); ++index) {
        const auto& original = with_copy[index == 0 ? index : index + 1];
        Check(original.id == items[index].id && original.id != with_copy[1].id);
        Check(EncodeActionSnapshot(original) == EncodeActionSnapshot(items[index]));
    }

    ActionRepository failing_write(path, [](const auto&, const auto&, std::string& message) {
        message = "Write failed";
        return false;
    });
    Check(failing_write.Load(error));
    Check(!failing_write.RenameItem("context-group", "context-page", "action-3", "Unsaved", error));
    Check(failing_write.Data().groups[0].pages[0].items[3].title == "Target 3 renamed");
    Check(ActionSerializer::Load(path, saved, error));
    Check(saved.groups[0].pages[0].items[3].title == "Target 3 renamed");

    const auto empty_items = Access::SelectPageAndRenderItems(workspace, "empty-page");
    Check(mouse(Mouse::Right, Mouse::Pressed, empty_items.x_min + 1, empty_items.y_min));
    Check(workspace.HasOpenContextMenu());
    Check(draw().ToString().find("Paste") != std::string::npos);
    Check(workspace.OnEvent(Event::Return));
    auto pasted_into_empty = workspace.Groups()[0].pages[1].items;
    Check(pasted_into_empty.size() == 1 && EncodeActionSnapshot(pasted_into_empty[0]) == snapshot);
    clipboard_text = eight_tab_snapshot;
    draw();
    open(pasted_into_empty.front().id);
    menu = FindActionsMenu(draw());
    click_row(menu, 10);  // Paste the eight-tab action after the copied action.
    pasted_into_empty = workspace.Groups()[0].pages[1].items;
    Check(pasted_into_empty.size() == 2 &&
        EncodeActionSnapshot(pasted_into_empty[1]) == eight_tab_snapshot);
}

} // namespace

int main() {
    TestHierarchicalActionsSidebar();
    ActionItem submitted;
    textlt::ActionEditorModal editor(nullptr,
        [&](const ActionItem& item, std::string&) {
            submitted = item;
            return true;
        });
    editor.Open();
    Check(textlt::ActionEditorModalTestAccess::TitleFocused(editor));
    Check(editor.OnEvent(ftxui::Event::Character("Build")));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).title == "Build");
    Check(editor.OnEvent(ftxui::Event::Character(std::string(80, 'x'))));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).title.size() ==
        kActionMetadataCharacterLimit);
    textlt::ActionEditorModalTestAccess::Render(editor);
    Check(textlt::ActionEditorModalTestAccess::ClearTitle(editor));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).title.empty());
    Check(textlt::ActionEditorModalTestAccess::TitleFocused(editor));
    Check(editor.OnEvent(ftxui::Event::Character("Build")));
    textlt::ActionEditorModalTestAccess::FocusCaption(editor);
    Check(editor.OnEvent(ftxui::Event::Character(std::string(80, 'C'))));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).tabs[0].caption.size() ==
        kActionCaptionCharacterLimit);
    textlt::ActionEditorModalTestAccess::Render(editor);
    Check(textlt::ActionEditorModalTestAccess::ClearDescription(editor));
    Check(editor.OnEvent(ftxui::Event::Character("Description")));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).tabs[0].caption == "Description");
    textlt::ActionEditorModalTestAccess::Render(editor);
    Check(textlt::ActionEditorModalTestAccess::ClearDescription(editor));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).tabs[0].caption.empty());
    Check(editor.OnEvent(ftxui::Event::Character("Description")));
    textlt::ActionEditorModalTestAccess::FocusContent(editor);
    Check(editor.OnEvent(ftxui::Event::Character("echo Привіт")));
    Check(editor.OnEvent(ftxui::Event::Return));
    Check(editor.OnEvent(ftxui::Event::Character("第二行")));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).tabs[0].content ==
        "echo Привіт\n第二行");
    textlt::ActionEditorModalTestAccess::Render(editor);
    Check(textlt::ActionEditorModalTestAccess::ClearContent(editor));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).tabs[0].content.empty());
    Check(editor.OnEvent(ftxui::Event::Character("Replacement")));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).tabs[0].content == "Replacement");
    textlt::ActionEditorModalTestAccess::SelectTab(editor, 1);
    textlt::ActionEditorModalTestAccess::FocusCaption(editor);
    Check(editor.OnEvent(ftxui::Event::Character("Alternate")));
    textlt::ActionEditorModalTestAccess::FocusContent(editor);
    Check(editor.OnEvent(ftxui::Event::Character("alternate content")));
    textlt::ActionEditorModalTestAccess::SelectTab(editor, 2);
    textlt::ActionEditorModalTestAccess::FocusCaption(editor);
    Check(editor.OnEvent(ftxui::Event::Character("Test")));
    textlt::ActionEditorModalTestAccess::FocusContent(editor);
    Check(editor.OnEvent(ftxui::Event::Character("test content")));
    textlt::ActionEditorModalTestAccess::SelectTab(editor, 3);
    textlt::ActionEditorModalTestAccess::FocusCaption(editor);
    Check(editor.OnEvent(ftxui::Event::Character("Production")));
    textlt::ActionEditorModalTestAccess::FocusContent(editor);
    Check(editor.OnEvent(ftxui::Event::Character("production content")));
    textlt::ActionEditorModalTestAccess::SelectTab(editor, 4);
    textlt::ActionEditorModalTestAccess::FocusCaption(editor);
    Check(editor.OnEvent(ftxui::Event::Character("Staging")));
    textlt::ActionEditorModalTestAccess::FocusContent(editor);
    Check(editor.OnEvent(ftxui::Event::Character("staging content")));
    textlt::ActionEditorModalTestAccess::SelectTab(editor, 5);
    textlt::ActionEditorModalTestAccess::FocusCaption(editor);
    Check(editor.OnEvent(ftxui::Event::Character("Preview")));
    textlt::ActionEditorModalTestAccess::FocusContent(editor);
    Check(editor.OnEvent(ftxui::Event::Character("preview content")));
    textlt::ActionEditorModalTestAccess::SelectTab(editor, 6);
    textlt::ActionEditorModalTestAccess::FocusCaption(editor);
    Check(editor.OnEvent(ftxui::Event::Character("QA")));
    textlt::ActionEditorModalTestAccess::FocusContent(editor);
    Check(editor.OnEvent(ftxui::Event::Character("qa content")));
    textlt::ActionEditorModalTestAccess::SelectTab(editor, 7);
    textlt::ActionEditorModalTestAccess::FocusCaption(editor);
    Check(editor.OnEvent(ftxui::Event::Character("Hotfix")));
    textlt::ActionEditorModalTestAccess::FocusContent(editor);
    Check(editor.OnEvent(ftxui::Event::Character("hotfix content")));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).tabs[1].caption == "Alternate");
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).tabs[1].content == "alternate content");
    textlt::ActionEditorModalTestAccess::Render(editor);
    Check(textlt::ActionEditorModalTestAccess::ActiveTab(editor) == 7);
    const std::string eight_tab_modal = textlt::ActionEditorModalTestAccess::RenderText(editor);
    Check(eight_tab_modal.find("Tab 1") != std::string::npos);
    Check(eight_tab_modal.find("Tab 8") != std::string::npos);
    Check(eight_tab_modal.find("Tabs 2–8 are optional") != std::string::npos);
    const auto modal_size =
        textlt::ActionEditorModalTestAccess::SizePreference(editor);
    Check(modal_size.width >= 100 && modal_size.height ==
        textlt::ActionEditorModalTestAccess::ExpectedPreferenceHeight(37));
    const auto tab_eight_box = textlt::ActionEditorModalTestAccess::TabBox(editor, 7);
    ftxui::Mouse tab_eight_click;
    tab_eight_click.button = ftxui::Mouse::Left;
    tab_eight_click.motion = ftxui::Mouse::Pressed;
    tab_eight_click.x = (tab_eight_box.x_min + tab_eight_box.x_max) / 2;
    tab_eight_click.y = (tab_eight_box.y_min + tab_eight_box.y_max) / 2;
    Check(editor.OnEvent(ftxui::Event::Mouse("", tab_eight_click)));
    Check(textlt::ActionEditorModalTestAccess::ActiveTab(editor) == 7);
    Check(textlt::ActionEditorModalTestAccess::Submit(editor));
    Check(submitted.tabs.size() == kMaxActionTabs);
    Check(submitted.tabs[7].caption == "Hotfix" &&
        submitted.tabs[7].content == "hotfix content");
    editor.Close();
    editor.Open(&submitted);
    textlt::ActionEditorModalTestAccess::SelectTab(editor, 7);
    textlt::ActionEditorModalTestAccess::FocusContent(editor);
    Check(editor.OnEvent(ftxui::Event::Character(" edited")));
    Check(textlt::ActionEditorModalTestAccess::Submit(editor));
    Check(submitted.tabs.size() == kMaxActionTabs &&
        submitted.tabs[7].content == "hotfix content edited");
    editor.Close();
    const ActionItem single_tab("one", "Single", "Only", "one body");
    editor.Open(&single_tab);
    textlt::ActionEditorModalTestAccess::Render(editor);
    Check(textlt::ActionEditorModalTestAccess::
        ContentScrollbarBox(editor, 0).IsEmpty());
    Check(textlt::ActionEditorModalTestAccess::Submit(editor));
    Check(submitted.tabs.size() == 1);
    editor.Close();
    ActionEditorModal spacing_editor(nullptr,
        [](const ActionItem&, std::string&) { return true; });
    spacing_editor.Open();
    const std::string spacing_modal =
        ActionEditorModalTestAccess::RenderModalText(spacing_editor);
    std::vector<std::string> modal_rows;
    size_t row_start = 0;
    while (row_start < spacing_modal.size()) {
        const size_t row_end = spacing_modal.find('\n', row_start);
        modal_rows.push_back(spacing_modal.substr(row_start,
            row_end == std::string::npos ? std::string::npos : row_end - row_start));
        if (row_end == std::string::npos) break;
        row_start = row_end + 1;
    }
    size_t checkbox_row = modal_rows.size();
    size_t footer_row = modal_rows.size();
    for (size_t row = 0; row < modal_rows.size(); ++row) {
        if (modal_rows[row].find("Hide text") != std::string::npos) checkbox_row = row;
        if (modal_rows[row].find("Cancel") != std::string::npos) footer_row = row;
    }
    Check(checkbox_row < modal_rows.size() && footer_row == checkbox_row + 3);
    const std::string compact_modal =
        ActionEditorModalTestAccess::RenderModalText(spacing_editor, 22);
    Check(compact_modal.find("Cancel") != std::string::npos &&
        compact_modal.find("Add") != std::string::npos);
    spacing_editor.Close();

    ActionEditorModal scrollbar_editor(nullptr,
        [](const ActionItem&, std::string&) { return true; });
    scrollbar_editor.Open();
    ActionEditorModalTestAccess::SetTab(scrollbar_editor, 0, "Long",
        std::string(180, 'x'), false);
    ActionEditorModalTestAccess::Render(scrollbar_editor);
    Check(ActionEditorModalTestAccess::SizePreference(
        scrollbar_editor).height ==
        ActionEditorModalTestAccess::ExpectedPreferenceHeight(38));
    const ftxui::Box content_scrollbar =
        ActionEditorModalTestAccess::ContentScrollbarBox(scrollbar_editor, 0);
    Check(!content_scrollbar.IsEmpty());
    Check(ActionEditorModalTestAccess::ContentScrollbarImmediatelyFollowsInput(
        scrollbar_editor, 0));
    ftxui::Mouse scroll_click;
    scroll_click.button = ftxui::Mouse::Left;
    scroll_click.motion = ftxui::Mouse::Pressed;
    scroll_click.x = content_scrollbar.x_max - 1;
    scroll_click.y = content_scrollbar.y_min;
    Check(scrollbar_editor.OnEvent(ftxui::Event::Mouse("", scroll_click)));
    Check(ActionEditorModalTestAccess::ContentCursor(scrollbar_editor, 0) > 0);
    ActionEditorModalTestAccess::Render(scrollbar_editor);
    const ftxui::Box start_scrollbar =
        ActionEditorModalTestAccess::ContentScrollbarBox(scrollbar_editor, 0);
    scroll_click.x = start_scrollbar.x_min;
    scroll_click.y = start_scrollbar.y_min;
    Check(scrollbar_editor.OnEvent(ftxui::Event::Mouse("", scroll_click)));
    Check(ActionEditorModalTestAccess::ContentCursor(scrollbar_editor, 0) == 0);
    scrollbar_editor.Close();

    const auto root = std::filesystem::temp_directory_path() / ("textlt-actions-ui-" + GenerateActionId());
    {
        const auto eight_tab_path = root / "eight-tabs.json";
        ActionsDocument empty;
        empty.groups.push_back({"six-group", "Six", std::nullopt, false,
            {{"six-page", "Six page", {}}}});
        std::string error;
        Check(ActionSerializer::Save(eight_tab_path, empty, error));
        std::string copied_text;
        ActionsWorkspaceComponent eight_tab_workspace(nullptr,
            [](const std::string&) {},
            [&](const std::string& content) {
                copied_text = content;
                return true;
            }, [] {}, eight_tab_path);
        eight_tab_workspace.SelectPage("six-group", "six-page");
        eight_tab_workspace.AddAction();
        Check(eight_tab_workspace.OnEvent(ftxui::Event::Character("Eight tab action")));
        auto& modal = ActionsWorkspaceModalTestAccess::ActionEditor(eight_tab_workspace);
        for (size_t index = 0; index < kMaxActionTabs; ++index) {
            ActionEditorModalTestAccess::SetTab(modal, index,
                "Tab " + std::to_string(index + 1),
                "clipboard " + std::to_string(index + 1), index == 5);
        }
        Check(ActionEditorModalTestAccess::Submit(modal));
        modal.Close();
        const auto created = eight_tab_workspace.Groups()[0].pages[0].items[0];
        Check(created.tabs.size() == kMaxActionTabs);
        Check(created.tabs[5].hide_text);
        Check(eight_tab_workspace.Save(error));
        ActionsDocument loaded;
        Check(ActionSerializer::Load(eight_tab_path, loaded, error));
        Check(loaded.groups[0].pages[0].items[0].tabs.size() == kMaxActionTabs);
        eight_tab_workspace.CopySelectedAction(7);
        Check(copied_text == "clipboard 8");
        eight_tab_workspace.EditAction();
        auto& edit_modal = ActionsWorkspaceModalTestAccess::ActionEditor(
            eight_tab_workspace);
        ActionEditorModalTestAccess::SetTab(edit_modal, 7, "Hotfix edit",
            "edited clipboard 8", false);
        Check(ActionEditorModalTestAccess::Submit(edit_modal));
        edit_modal.Close();
        Check(eight_tab_workspace.Groups()[0].pages[0].items[0].tabs[7].caption ==
            "Hotfix edit");
        eight_tab_workspace.CopySelectedAction(7);
        Check(copied_text == "edited clipboard 8");
    }
    TestActionsContextMenu(root / "context-menu.json");
    const auto path = root / "actions.json";
    std::string unicode_metadata;
    for (int index = 0; index < 55; ++index) unicode_metadata += "Ж";
    std::string expected_unicode_metadata;
    for (size_t index = 0; index < kActionMetadataCharacterLimit; ++index)
        expected_unicode_metadata += "Ж";
    Check(LimitActionMetadata(unicode_metadata) == expected_unicode_metadata);

    const std::string long_title(70, 'T');
    const std::string long_description(70, 'D');
    const std::string long_content =
        "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8";
    ActionsDocument data;
    data.groups.push_back({"group", "Group", std::nullopt, false, {
        {"page", "A page name long enough to wrap across multiple lines", {
            {"one", "One", "", "echo Привіт\r\nline two"},
            {"two", "Two", "", "配置\nvalue"},
        }}
    }});
    data.groups.push_back({"group-two", "Second", std::nullopt, false, {
        {"page-two", "Second page", {
            ActionItem::WithTabs("long", long_title, {
                {long_description, long_content, false},
                {"Release", "release content", false},
                {"Test", "test content", false},
                {"Production", "production content", false},
                {"Staging", "staging content", false},
                {"Preview", "preview content", true},
                {"QA", "qa content", false},
                {"Hotfix", "hotfix content", false},
            }),
            {"more-1", "More 1", "", "content 1"},
            {"more-2", "More 2", "", "content 2"},
            {"more-3", "More 3", "", "content 3"},
            {"more-4", "More 4", "", "content 4"},
            {"more-5", "More 5", "", "content 5"}
        }}
    }});
    std::string error; Check(ActionSerializer::Save(path, data, error));
    std::string copied;
    ActionsWorkspaceComponent workspace(nullptr, [](const std::string&) {},
        [&](const std::string& text) { copied = text; return true; }, [] {}, path);
    workspace.SelectGroup("group");
    Check(workspace.SelectedGroupId() == "group");
    Check(workspace.SelectedPageId().empty() && !workspace.HasSelectedPage());
    workspace.SelectPage("group", "page");
    Check(workspace.SelectedPageId() == "page" && workspace.HasSelectedPage());
    workspace.AddAction();
    Check(workspace.OnEvent(ftxui::Event::Character("Typed through workspace")));
    Check(ActionsWorkspaceModalTestAccess::Draft(workspace).title ==
        "Typed through workspace");
    workspace.OnEvent(ftxui::Event::Escape);
    workspace.CopySelectedAction();
    Check(copied == "echo Привіт\r\nline two");
    workspace.CopyAllActions();
    Check(copied == "echo Привіт\r\nline two\n\n配置\nvalue");
    workspace.OnEvent(ftxui::Event::Tab);
    workspace.OnEvent(ftxui::Event::ArrowDown);
    workspace.CopySelectedAction();
    Check(copied == "配置\nvalue");
    // Establish the first page's viewport before switching to a page whose
    // taller cards require scrolling.
    ActionsWorkspaceModalTestAccess::RenderText(workspace);
    workspace.SelectPage("group-two", "page-two");
    Check(workspace.SelectedGroupId() == "group-two");
    const std::string rendered =
        ActionsWorkspaceModalTestAccess::RenderText(workspace, 100, 30);
    Check(ActionsWorkspaceModalTestAccess::ItemsNeedScrolling(workspace));
    Check(ActionsWorkspaceModalTestAccess::ItemsScrollbarVisible(workspace));
    Check(ActionsWorkspaceModalTestAccess::WheelItems(
        workspace, ftxui::Mouse::WheelDown));
    Check(ActionsWorkspaceModalTestAccess::ItemScroll(workspace) > 0);
    Check(ActionsWorkspaceModalTestAccess::WheelItems(
        workspace, ftxui::Mouse::WheelUp));
    Check(ActionsWorkspaceModalTestAccess::ItemScroll(workspace) == 0);
    Check(rendered.find("Groups") == std::string::npos);
    Check(rendered.find("Pages") == std::string::npos);
    Check(rendered.find("Group: Second") == std::string::npos);
    Check(rendered.find("Second page") != std::string::npos);
    Check(rendered.find(std::string(25, 'T')) != std::string::npos);
    Check(rendered.find(std::string(26, 'T')) == std::string::npos);
    Check(rendered.find("line 5") != std::string::npos);
    Check(rendered.find("line 6") == std::string::npos);
    Check(ActionsWorkspaceModalTestAccess::FirstItemHeight(workspace) <= 32);
    Check(ActionsWorkspaceModalTestAccess::FirstActionCopyButtonsDoNotOverlap(workspace));
    Check(ActionsWorkspaceModalTestAccess::FirstItemFillsCardWidth(workspace));
    Check(ActionsWorkspaceModalTestAccess::FirstActionMarkerAndHeaderStyleAreCorrect(workspace));
    Check(ActionsWorkspaceModalTestAccess::FirstCopyButtonVisible(workspace));
    ActionsWorkspaceModalTestAccess::RenderText(workspace, 100, 60);
    Check(ActionsWorkspaceModalTestAccess::FirstActionCopyButtonCount(workspace) == 8);
    Check(ActionsWorkspaceModalTestAccess::FirstActionCopyButtonsEqualWidth(workspace));
    ActionsWorkspaceModalTestAccess::RenderText(workspace, 140, 60);
    Check(ActionsWorkspaceModalTestAccess::
        FirstActionCopyButtonsShareOneRow(workspace));
    Check(ActionsWorkspaceModalTestAccess::ClickFirstActionCopyButton(workspace, 1));
    Check(copied == "release content");
    Check(ActionsWorkspaceModalTestAccess::ClickFirstActionCopyButton(workspace, 5));
    Check(copied == "preview content");
    Check(ActionsWorkspaceModalTestAccess::RenderText(workspace, 100, 60).find(
        "preview content") == std::string::npos);
    Check(ActionsWorkspaceModalTestAccess::ClickFirstActionCopyButton(workspace, 1));
    Check(copied == "release content");
    Check(ActionsWorkspaceModalTestAccess::RenderText(workspace, 100, 60).find(
        "release content") != std::string::npos);
    Check(ActionsWorkspaceModalTestAccess::ClickFirstActionCopyButton(workspace, 7));
    Check(copied == "hotfix content");
    Check(ActionsWorkspaceModalTestAccess::RenderText(workspace, 100, 60).find(
        "hotfix content") != std::string::npos);
    bool search_truncated = false;
    auto tab_matches = workspace.SearchActions("Release", true, false, 10,
        search_truncated);
    Check(!search_truncated && tab_matches.size() == 1);
    Check(tab_matches.front().field == ActionField::Caption);
    Check(tab_matches.front().tab_index == 1);
    Check(tab_matches.front().label.find("tab 2") != std::string::npos);
    tab_matches = workspace.SearchActions("release content", true, false, 10,
        search_truncated);
    Check(tab_matches.size() == 1);
    Check(tab_matches.front().field == ActionField::Content);
    Check(tab_matches.front().tab_index == 1);
    workspace.ClearActionSearchHighlights();
    const std::string narrow_rendered =
        ActionsWorkspaceModalTestAccess::RenderText(workspace, 30);
    Check(ActionsWorkspaceModalTestAccess::FirstCopyButtonVisible(workspace));
    Check(narrow_rendered.find("Release") != std::string::npos);
    Check(ActionsWorkspaceModalTestAccess::FirstItemHeight(workspace) <= 32);
    Check(ActionsWorkspaceModalTestAccess::FirstActionCopyButtonsDoNotOverlap(workspace));
    ActionsWorkspaceModalTestAccess::RenderText(workspace, 30);
    Check(ActionsWorkspaceModalTestAccess::ItemsScrollbarVisible(workspace));
    workspace.CopySelectedAction();
    Check(copied == long_content);
    workspace.NewPage();
    Check(ActionsWorkspaceModalTestAccess::NewPageDialogOpen(workspace));
    Check(workspace.OnEvent(ftxui::Event::Escape));
    workspace.NewGroup();
    Check(ActionsWorkspaceModalTestAccess::NewGroupDialogOpen(workspace));
    Check(workspace.OnEvent(ftxui::Event::Escape));

    const auto auto_pages_path = root / "automatic-pages.json";
    ActionsDocument auto_pages;
    auto_pages.groups = {
        {"auto-first", "First", std::nullopt, false, {}},
        {"auto-second", "Second", std::nullopt, false, {}},
        {"auto-third", "Third", std::nullopt, false, {}},
        {"other", "Other", std::nullopt, false, {{"existing", "page3", {}}}},
    };
    Check(ActionSerializer::Save(auto_pages_path, auto_pages, error));
    ActionsWorkspaceComponent auto_workspace(nullptr, [](const std::string&) {},
        [](const std::string&) { return true; }, [] {}, auto_pages_path);
    auto_workspace.AddAction();
    Check(auto_workspace.OnEvent(ftxui::Event::Escape));
    Check(!auto_workspace.HasSelectedPage());
    Check(auto_workspace.Groups().front().pages.empty());
    Check(ActionsWorkspaceModalTestAccess::SaveNewAction(auto_workspace, "First action"));
    Check(auto_workspace.HasSelectedPage());
    Check(ActionsWorkspaceModalTestAccess::SaveNewAction(auto_workspace, "Same page"));
    auto_workspace.SelectGroup("auto-second");
    Check(ActionsWorkspaceModalTestAccess::SaveNewAction(auto_workspace, "Second action"));
    auto_workspace.SelectGroup("auto-third");
    Check(ActionsWorkspaceModalTestAccess::SaveNewAction(auto_workspace, "Fourth page"));

    ActionsDocument persisted_pages;
    Check(ActionSerializer::Load(auto_pages_path, persisted_pages, error));
    const auto& groups = persisted_pages.groups;
    Check(groups[0].pages.size() == 1 && groups[0].pages[0].name == "Page1");
    Check(groups[0].pages[0].items.size() == 2);
    Check(groups[1].pages[0].name == "Page2");
    Check(groups[1].pages[0].items.front().title == "Second action");
    Check(groups[2].pages[0].name == "Page4");

    const auto orphan_path = root / "orphan-actions.json";
    ActionsDocument orphan_actions;
    orphan_actions.groups = {
        {"linked-group", "Linked", "existing-project", true, {}},
        {"other-orphan", "Other", "missing-project", true, {}},
        {"orphan-group", "Orphan", "missing-project", true,
            {{"page", "Page", {{"action", "Build", "", "make"}}}}},
    };
    Check(ActionSerializer::Save(orphan_path, orphan_actions, error));
    ActionsWorkspaceComponent orphan_workspace(nullptr, [](const std::string&) {},
        [](const std::string&) { return true; }, [] {}, orphan_path);
    orphan_workspace.SetMissingProjectCheck(
        [](const std::string& id) { return id == "missing-project"; });
    orphan_workspace.DeleteGroup();
    Check(ActionsWorkspaceModalTestAccess::DialogIsNone(orphan_workspace));
    orphan_workspace.SelectGroup("orphan-group");
    orphan_workspace.DeleteGroup();
    Check(ActionsWorkspaceModalTestAccess::DeleteGroupDialogOpen(orphan_workspace));
    Check(orphan_workspace.OnEvent(ftxui::Event::Escape));
    const auto detached_groups = orphan_workspace.Groups();
    Check(detached_groups[0].project_id.has_value());
    Check(detached_groups[1].project_id.has_value());
    Check(!detached_groups[2].project_id && !detached_groups[2].auto_named);
    Check(detached_groups[2].pages.front().items.front().id == "action");
    orphan_workspace.DeleteGroup();
    Check(orphan_workspace.OnEvent(ftxui::Event::Return));
    Check(orphan_workspace.Groups().size() == 2);
    std::filesystem::remove_all(root);

    // Merge group: similar-named groups are merged, clashing pages renumbered.
    {
        const auto merge_root =
            std::filesystem::temp_directory_path() / ("textlt-actions-merge-ui-" + GenerateActionId());
        const auto merge_path = merge_root / "actions.json";
        std::string merge_error;
        ActionsDocument merge_data;
        merge_data.groups.push_back({"g-deploy", "Deploy", std::nullopt, false, {
            {"p-main", "Main", {{"i-one", "one", "", "content one"}}}
        }});
        merge_data.groups.push_back({"g-deploy-1", "Deploy (1)", std::nullopt, false, {
            {"p-main-1", "Main", {{"i-two", "two", "", "content two"}}},
            {"p-extra", "Extra", {{"i-three", "three", "", "content three"}}}
        }});
        Check(ActionSerializer::Save(merge_path, merge_data, merge_error));
        ActionsWorkspaceComponent merge_workspace(nullptr, [](const std::string&) {},
            [&](const std::string& text) { copied = text; return true; }, [] {}, merge_path);
        merge_workspace.SelectGroup("g-deploy-1");
        merge_workspace.MergeGroup();
        Check(ActionsWorkspaceModalTestAccess::MergeGroupDialogOpen(merge_workspace));
        Check(ActionsWorkspaceModalTestAccess::MergeTargetId(merge_workspace) == "g-deploy");
        // Confirm the merge (Enter).
        Check(merge_workspace.OnEvent(ftxui::Event::Return));
        Check(ActionsWorkspaceModalTestAccess::DialogIsNone(merge_workspace));
        const auto merged_groups = merge_workspace.Groups();
        Check(merged_groups.size() == 1);
        const auto& merged = merged_groups.front();
        Check(merged.name == "Deploy");
        std::vector<std::string> page_names;
        for (const auto& pg : merged.pages) page_names.push_back(pg.name);
        std::vector<std::string> expected_pages = {"Main", "Main (1)", "Extra"};
        Check(page_names == expected_pages);
        bool has_one = false, has_two = false;
        for (const auto& pg : merged.pages) {
            for (const auto& item : pg.items) {
                if (pg.name == "Main" && item.tabs.front().content == "content one") has_one = true;
                if (pg.name == "Main (1)" && item.tabs.front().content == "content two") has_two = true;
            }
        }
        Check(has_one && has_two);
        std::filesystem::remove_all(merge_root);
    }

    // Merge group with no similar group: notifies, no dialog.
    {
        const auto merge_root =
            std::filesystem::temp_directory_path() / ("textlt-actions-merge-none-ui-" + GenerateActionId());
        const auto merge_path = merge_root / "actions.json";
        std::string merge_error;
        ActionsDocument merge_data;
        merge_data.groups.push_back({"g-alpha", "Alpha", std::nullopt, false, {}});
        merge_data.groups.push_back({"g-beta", "Beta", std::nullopt, false, {}});
        Check(ActionSerializer::Save(merge_path, merge_data, merge_error));
        ActionsWorkspaceComponent merge_workspace(nullptr, [](const std::string&) {},
            [](const std::string&) { return true; }, [] {}, merge_path);
        merge_workspace.SelectGroup("g-alpha");
        merge_workspace.MergeGroup();
        Check(ActionsWorkspaceModalTestAccess::DialogIsNone(merge_workspace));
        Check(merge_workspace.Groups().size() == 2);
        std::filesystem::remove_all(merge_root);
    }
}
