#include <cstdlib>
#include <filesystem>
#include <string>

#include "actions/action_serializer.hpp"
#include "actions/actions_workspace_component.hpp"
#include "ftxui/component/event.hpp"
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
};
} // namespace textlt

namespace textlt::actions {
class ActionsWorkspaceModalTestAccess {
public:
    static ActionItem Draft(const ActionsWorkspaceComponent& workspace) {
        return textlt::ActionEditorModalTestAccess::Draft(
            *workspace.action_editor_modal_);
    }
};
} // namespace textlt::actions

static void Check(bool condition) { if (!condition) std::abort(); }

int main() {
    textlt::ActionEditorModal editor(nullptr,
        [](const ActionItem&, std::string&) { return true; });
    editor.Open();
    Check(textlt::ActionEditorModalTestAccess::TitleFocused(editor));
    Check(editor.OnEvent(ftxui::Event::Character("Build")));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).title == "Build");
    Check(editor.OnEvent(ftxui::Event::Tab));
    Check(editor.OnEvent(ftxui::Event::Character("Description")));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).description == "Description");
    Check(editor.OnEvent(ftxui::Event::Tab));
    Check(editor.OnEvent(ftxui::Event::Character("echo Привіт")));
    Check(editor.OnEvent(ftxui::Event::Return));
    Check(editor.OnEvent(ftxui::Event::Character("第二行")));
    Check(textlt::ActionEditorModalTestAccess::Draft(editor).content ==
        "echo Привіт\n第二行");
    editor.Close();

    const auto root = std::filesystem::temp_directory_path() / ("textlt-actions-ui-" + GenerateActionId());
    const auto path = root / "actions.json";
    ActionsDocument data;
    data.groups.push_back({"group", "Group", std::nullopt, false, {
        {"page", "A page name long enough to wrap across multiple lines", {
            {"one", "One", "", "echo Привіт\r\nline two"},
            {"two", "Two", "", "配置\nvalue"},
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
    std::filesystem::remove_all(root);
}
