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
        return {
            modal.content_->id_,
            modal.content_->title_,
            modal.content_->description_,
            modal.content_->content_,
        };
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
        return ClickClear(modal, modal.content_->description_clear_box_);
    }
    static bool ClearContent(ActionEditorModal& modal) {
        return ClickClear(modal, modal.content_->content_clear_box_);
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
    static int FirstItemHeight(const ActionsWorkspaceComponent& workspace) {
        if (workspace.item_boxes_.empty()) return 0;
        const ftxui::Box& box = workspace.item_boxes_.front();
        return box.y_max - box.y_min + 1;
    }
    static bool FirstCopyButtonVisible(const ActionsWorkspaceComponent& workspace) {
        if (workspace.copy_boxes_.empty()) return false;
        const ftxui::Box& box = workspace.copy_boxes_.front();
        return workspace.items_box_.Contain(box.x_min, box.y_min) &&
            workspace.items_box_.Contain(box.x_max, box.y_max);
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
    textlt::ActionEditorModal editor(nullptr,
        [](const ActionItem&, std::string&) { return true; });
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
    Check(editor.OnEvent(ftxui::Event::Tab));
    Check(editor.OnEvent(ftxui::Event::Character("Description")));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).description == "Description");
    textlt::ActionEditorModalTestAccess::Render(editor);
    Check(textlt::ActionEditorModalTestAccess::ClearDescription(editor));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).description.empty());
    Check(editor.OnEvent(ftxui::Event::Character("Description")));
    Check(editor.OnEvent(ftxui::Event::Tab));
    Check(editor.OnEvent(ftxui::Event::Character("echo Привіт")));
    Check(editor.OnEvent(ftxui::Event::Return));
    Check(editor.OnEvent(ftxui::Event::Character("第二行")));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).content ==
        "echo Привіт\n第二行");
    textlt::ActionEditorModalTestAccess::Render(editor);
    Check(textlt::ActionEditorModalTestAccess::ClearContent(editor));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).content.empty());
    Check(editor.OnEvent(ftxui::Event::Character("Replacement")));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).content == "Replacement");
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
            {"long", long_title, long_description, long_content},
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
    Check(ActionsWorkspaceModalTestAccess::FirstCopyButtonVisible(workspace));
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
}
