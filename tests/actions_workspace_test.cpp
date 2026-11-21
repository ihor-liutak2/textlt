#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>

#include "actions/action_serializer.hpp"
#include "actions/actions_workspace_component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "modals/modal_action_editor.hpp"

using namespace textlt;
using namespace textlt::actions;

namespace textlt {
class ActionEditorModalTestAccess {
public:
    static actions::ActionItem Draft(const ActionEditorModal& modal) {
        return actions::ActionItem::WithTabs(modal.content_->id_,
            modal.content_->title_, {
                {modal.content_->captions_[0], modal.content_->contents_[0],
                    modal.content_->hide_text_[0]},
                {modal.content_->captions_[1], modal.content_->contents_[1],
                    modal.content_->hide_text_[1]},
                {modal.content_->captions_[2], modal.content_->contents_[2],
                    modal.content_->hide_text_[2]},
                {modal.content_->captions_[3], modal.content_->contents_[3],
                    modal.content_->hide_text_[3]},
            });
    }
    static bool TitleFocused(const ActionEditorModal& modal) {
        return modal.content_->title_input_->Focused();
    }
    static void Render(ActionEditorModal& modal) {
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(30));
        ftxui::Render(screen, modal.content_->Render());
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
    static std::string RenderText(
        ActionsWorkspaceComponent& workspace, int width = 100) {
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(24));
        ftxui::Render(screen, workspace.Render());
        return screen.ToString();
    }
    static bool ClickNavigatorNew(ActionsWorkspaceComponent& workspace) {
        const ftxui::Box box = workspace.navigator_new_box_;
        ftxui::Mouse mouse;
        mouse.button = ftxui::Mouse::Left;
        mouse.motion = ftxui::Mouse::Pressed;
        mouse.x = (box.x_min + box.x_max) / 2;
        mouse.y = (box.y_min + box.y_max) / 2;
        return workspace.OnEvent(ftxui::Event::Mouse("", mouse));
    }
    static bool NewPageDialogOpen(const ActionsWorkspaceComponent& workspace) {
        return workspace.dialog_ == ActionsWorkspaceComponent::Dialog::NewPage;
    }
    static bool NewGroupDialogOpen(const ActionsWorkspaceComponent& workspace) {
        return workspace.dialog_ == ActionsWorkspaceComponent::Dialog::NewGroup;
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
            for (int x = item.x_max - 1; x <= item.x_max; ++x) {
                const ftxui::Cell& marker = screen.CellAt(x, y);
                if (marker.character != "█" ||
                    marker.foreground_color != theme.modal_accent) return false;
                if (y == item.y_min &&
                    marker.background_color != theme.modal_selected_item_bg) return false;
            }
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
    static bool ClickFirstActionCopyButton(
        ActionsWorkspaceComponent& workspace, size_t tab_index) {
        if (workspace.copy_boxes_.empty() || tab_index >= 4) return false;
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
};
} // namespace textlt::actions

#define Check(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "Check failed at %s:%d: %s\n",               \
                __FILE__, __LINE__, #condition);                                \
            std::abort();                                                       \
        }                                                                       \
    } while (false)

int main() {
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
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).tabs[1].caption == "Alternate");
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).tabs[1].content == "alternate content");
    Check(textlt::ActionEditorModalTestAccess::Submit(editor));
    Check(submitted.tabs.size() == 4);
    editor.Close();
    const ActionItem single_tab("one", "Single", "Only", "one body");
    editor.Open(&single_tab);
    Check(textlt::ActionEditorModalTestAccess::Submit(editor));
    Check(submitted.tabs.size() == 1);
    editor.Close();

    const auto root = std::filesystem::temp_directory_path() / ("textlt-actions-ui-" + GenerateActionId());
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
    Check(workspace.OnEvent(ftxui::Event::Tab));
    Check(workspace.OnEvent(ftxui::Event::ArrowLeft));
    Check(workspace.OnEvent(ftxui::Event::ArrowDown));
    Check(workspace.SelectedGroupId() == "group-two");
    Check(workspace.OnEvent(ftxui::Event::ArrowRight));
    const std::string rendered =
        ActionsWorkspaceModalTestAccess::RenderText(workspace);
    Check(ActionsWorkspaceModalTestAccess::ItemsNeedScrolling(workspace));
    Check(ActionsWorkspaceModalTestAccess::ItemsScrollbarVisible(workspace));
    Check(ActionsWorkspaceModalTestAccess::WheelItems(
        workspace, ftxui::Mouse::WheelDown));
    Check(ActionsWorkspaceModalTestAccess::ItemScroll(workspace) > 0);
    Check(ActionsWorkspaceModalTestAccess::WheelItems(
        workspace, ftxui::Mouse::WheelUp));
    Check(ActionsWorkspaceModalTestAccess::ItemScroll(workspace) == 0);
    Check(rendered.find("Groups") != std::string::npos);
    Check(rendered.find("Pages") != std::string::npos);
    Check(rendered.find("Group: Second") != std::string::npos);
    Check(rendered.find("New") != std::string::npos);
    Check(rendered.find("Rename") != std::string::npos);
    Check(rendered.find("Delete") != std::string::npos);
    Check(rendered.find("New") < rendered.find("Group: Second"));
    Check(rendered.find(std::string(25, 'T')) != std::string::npos);
    Check(rendered.find(std::string(26, 'T')) == std::string::npos);
    Check(rendered.find("line 5") != std::string::npos);
    Check(rendered.find("line 6") == std::string::npos);
    Check(ActionsWorkspaceModalTestAccess::FirstItemHeight(workspace) <= 7);
    Check(ActionsWorkspaceModalTestAccess::FirstItemFillsCardWidth(workspace));
    Check(ActionsWorkspaceModalTestAccess::FirstActionMarkerAndHeaderStyleAreCorrect(workspace));
    Check(ActionsWorkspaceModalTestAccess::FirstCopyButtonVisible(workspace));
    Check(ActionsWorkspaceModalTestAccess::FirstActionCopyButtonCount(workspace) == 4);
    Check(ActionsWorkspaceModalTestAccess::FirstActionCopyButtonsEqualWidth(workspace));
    Check(ActionsWorkspaceModalTestAccess::ClickFirstActionCopyButton(workspace, 1));
    Check(copied == "release content");
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
        ActionsWorkspaceModalTestAccess::RenderText(workspace, 58);
    Check(ActionsWorkspaceModalTestAccess::FirstCopyButtonVisible(workspace));
    Check(narrow_rendered.find("Copy") != std::string::npos);
    Check(ActionsWorkspaceModalTestAccess::FirstItemHeight(workspace) <= 7);
    ActionsWorkspaceModalTestAccess::RenderText(workspace, 58);
    Check(ActionsWorkspaceModalTestAccess::ItemsScrollbarVisible(workspace));
    Check(ActionsWorkspaceModalTestAccess::WheelItems(
        workspace, ftxui::Mouse::WheelDown));
    Check(ActionsWorkspaceModalTestAccess::ItemScroll(workspace) > 0);
    workspace.CopySelectedAction();
    Check(copied == long_content);
    Check(ActionsWorkspaceModalTestAccess::ClickNavigatorNew(workspace));
    Check(ActionsWorkspaceModalTestAccess::NewPageDialogOpen(workspace));
    Check(workspace.OnEvent(ftxui::Event::Escape));
    Check(workspace.OnEvent(ftxui::Event::ArrowLeft));
    ActionsWorkspaceModalTestAccess::RenderText(workspace);
    Check(ActionsWorkspaceModalTestAccess::ClickNavigatorNew(workspace));
    Check(ActionsWorkspaceModalTestAccess::NewGroupDialogOpen(workspace));
    Check(workspace.OnEvent(ftxui::Event::Escape));
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
