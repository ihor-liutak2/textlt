#include "app.hpp"

namespace textlt {

void TextltApp::RefreshActionsSidebar() {
    if (!sidebar_panel_ || !actions_workspace_component_) return;
    auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    std::vector<SidebarPanel::ActionGroupEntry> entries;
    for (const auto& group : workspace->Groups())
        entries.push_back({group.id, group.name, group.project_id.has_value()});
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->SetActionGroups(
        std::move(entries), workspace->SelectedGroupId());
}

void TextltApp::SyncActionsProjectContext() {
    if (!actions_workspace_component_) return;
    std::optional<std::pair<std::string, std::string>> context;
    if (const ProjectRecord* project = ActiveWorkspaceProject()) context = {{project->id, project->name}};
    std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_)->SetProjectContext(context);
    RefreshActionsSidebar();
}

void TextltApp::ShowActionsWorkspace() {
    if (workspace_mode_ == WorkspaceMode::Actions) {
        // Re-selecting the active workspace must not reset focus, selection,
        // page state, or an open Action editor.
        RefreshActionsSidebar();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    if (workspace_mode_ == WorkspaceMode::Notes && notes_workspace_component_) {
        std::string error;
        std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->Save(error);
        if (!error.empty()) active_action_ = "Notes save failed: " + error;
    }
    if (layout_controller_.IsDistractionModeActive()) SetDistractionEnabled(false);
    CloseDropdown(); CloseFindPanel();
    workspace_mode_ = WorkspaceMode::Actions;
    workspace_mode_index_ = static_cast<int>(workspace_mode_);
    menu_bar_->SetNotesMenuVisible(false); menu_bar_->SetActionsMenuVisible(true);
    SyncActionsProjectContext(); RefreshActionsSidebar();
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_); sidebar->ShowActions();
    sidebar_has_focus_ = true; sidebar->FocusMenu();
    std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_)->Open();
    active_action_ = "Actions"; screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CommandActionsNewGroup() {
    if (workspace_mode_ != WorkspaceMode::Actions) ShowActionsWorkspace();
    sidebar_has_focus_ = false;
    auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    workspace->TakeFocus(); workspace->NewGroup();
}
void TextltApp::CommandActionsRenameGroup() {
    sidebar_has_focus_ = false;
    auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    workspace->TakeFocus(); workspace->RenameGroup();
}
void TextltApp::CommandActionsDeleteGroup() {
    sidebar_has_focus_ = false;
    auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    workspace->TakeFocus(); workspace->DeleteGroup();
}
void TextltApp::CommandActionsNewPage() {
    sidebar_has_focus_ = false;
    auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    workspace->TakeFocus(); workspace->NewPage();
}
void TextltApp::CommandActionsRenamePage() {
    sidebar_has_focus_ = false;
    auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    workspace->TakeFocus(); workspace->RenamePage();
}
void TextltApp::CommandActionsDeletePage() {
    sidebar_has_focus_ = false;
    auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    workspace->TakeFocus(); workspace->DeletePage();
}
void TextltApp::CommandActionsMovePage(int delta) { std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_)->MovePage(delta); }
void TextltApp::CommandActionsAdd() {
    sidebar_has_focus_ = false;
    auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    workspace->TakeFocus(); workspace->AddAction();
}
void TextltApp::CommandActionsEdit() {
    sidebar_has_focus_ = false;
    auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    workspace->TakeFocus(); workspace->EditAction();
}
void TextltApp::CommandActionsDelete() {
    sidebar_has_focus_ = false;
    auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    workspace->TakeFocus(); workspace->DeleteAction();
}
void TextltApp::CommandActionsMove(int delta) { std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_)->MoveAction(delta); }
void TextltApp::CommandActionsCopy() { std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_)->CopySelectedAction(); }
void TextltApp::CommandActionsCopyAll() { std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_)->CopyAllActions(); }

} // namespace textlt
