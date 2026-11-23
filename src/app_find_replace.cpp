#include "app.hpp"

#include <utility>

#include "find_replace/find_target.hpp"
#include "ftxui/component/event.hpp"

namespace textlt {

void TextltApp::RestoreNotesSidebarForFindPanel() {
    if (!find_panel_hid_notes_sidebar_) return;
    if (notes_workspace_component_) {
        auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(
            notes_workspace_component_);
        notes->SetSidebarVisible(find_panel_notes_sidebar_visible_);
    }
    find_panel_hid_notes_sidebar_ = false;
}

void TextltApp::SyncFindPanelTarget() {
    if (!find_replace_panel_) return;

    FindTarget* target = editor_find_target_.get();
    if (workspace_mode_ == WorkspaceMode::Notes) {
        target = notes_find_target_.get();
    } else if (workspace_mode_ == WorkspaceMode::Actions) {
        target = actions_find_target_.get();
    }

    if (find_panel_open_) {
        if (workspace_mode_ == WorkspaceMode::Notes &&
            notes_workspace_component_) {
            auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(
                notes_workspace_component_);
            if (notes->IsSidebarVisible()) {
                if (!find_panel_hid_notes_sidebar_) {
                    find_panel_notes_sidebar_visible_ = true;
                    find_panel_hid_notes_sidebar_ = true;
                    notes->SetSidebarVisible(false);
                }
            } else if (!find_panel_hid_notes_sidebar_) {
                find_panel_notes_sidebar_visible_ = false;
            }
            notes->FocusMainArea();
        } else {
            RestoreNotesSidebarForFindPanel();
            if (sidebar_panel_) {
                std::static_pointer_cast<SidebarPanel>(sidebar_panel_)
                    ->ClearHitboxes();
            }
        }
    }

    find_replace_panel_->SetTarget(target);
}

void TextltApp::OpenFindPanel(bool replace_tab, std::optional<std::string> query) {
    if (!find_replace_panel_) return;
    if (menu_bar_) menu_bar_->CloseDropdown();
    SetActiveLayer(UiLayer::Main);

    SyncFindPanelTarget();
    FindTarget* target = find_replace_panel_->Target();
    if (!target || !target->IsAvailable()) {
        active_action_ = target ? target->UnavailableMessage()
                                : "Find is unavailable";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    find_panel_open_ = true;
    find_panel_focus_ = true;
    sidebar_has_focus_ = false;
    SyncFindPanelTarget();

    target = find_replace_panel_->Target();
    if (!target || !target->IsAvailable()) {
        find_panel_open_ = false;
        active_action_ = target ? target->UnavailableMessage()
                                : "Find is unavailable";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    find_replace_panel_->Open(replace_tab ? FindReplaceTab::Replace
                                          : FindReplaceTab::Find, std::move(query));
    if (sidebar_panel_) {
        std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->ClearHitboxes();
    }
    active_action_ = replace_tab ? "Replace" : "Find";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CloseFindPanel() {
    if (!find_replace_panel_) return;
    find_replace_panel_->Close();
    find_panel_open_ = false;
    find_panel_focus_ = false;
    RestoreNotesSidebarForFindPanel();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

} // namespace textlt
