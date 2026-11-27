#pragma once

#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "actions/action_repository.hpp"
#include "actions/actions_context_menu.hpp"
#include "editor/clipboard_controller.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"
#include "modals/modal_action_editor.hpp"

namespace textlt {
struct InputSelection;
}

namespace textlt::actions {

class ActionsWorkspaceComponent : public ftxui::ComponentBase {
public:
    using StatusCallback = std::function<void(const std::string&)>;
    using ClipboardCallback = std::function<bool(const std::string&)>;
    using GroupsChangedCallback = std::function<void()>;
    using MissingProjectCheck = std::function<bool(const std::string&)>;

    ActionsWorkspaceComponent(const Theme* theme, StatusCallback status,
        ClipboardCallback clipboard, GroupsChangedCallback groups_changed,
        std::filesystem::path storage_path = {});
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    void SetClipboardReader(ClipboardController::ReadCallback reader) { read_clipboard_ = std::move(reader); }
    bool HasOpenContextMenu() const { return context_menu_.IsOpen(); }
    bool HandleContextMenuEvent(ftxui::Event event);
    bool Focusable() const override { return true; }
    void Open();
    bool Save(std::string& error) const;
    void SetProjectContext(std::optional<std::pair<std::string, std::string>> project);
    void SetMissingProjectCheck(MissingProjectCheck check) {
        is_missing_project_ = std::move(check);
    }
    bool UpdateProjectGroupName(const std::string& project_id,
        const std::string& project_name, std::string& error);
    bool MergeGroupsForNewProject(const std::string& project_id,
        const std::string& project_name, std::string& error);
    bool DetachProjectGroup(const std::string& project_id, std::string& error);

    std::vector<ActionGroup> Groups() const { return repository_.Data().groups; }
    std::string SelectedGroupId() const { return selected_group_id_; }
    std::string SelectedPageId() const { return selected_page_id_; }
    void SelectGroup(const std::string& id);
    void SelectPage(const std::string& group_id, const std::string& page_id);

    void NewGroup();
    void RenameGroup();
    void DeleteGroup();
    void MergeGroup();
    void NewPage();
    void RenamePage();
    void DeletePage();
    void MovePage(int delta);
    void AddAction();
    void EditAction();
    void DeleteAction();
    void MoveAction(int delta);
    void CopySelectedAction(int tab_index = 0);
    void CopyAllActions();
    bool HasSelectedPage() const;
    bool ImportActions(std::vector<ActionItem> items, std::string& error);
    bool ImportActionsTo(std::vector<ActionItem> items, const std::string& group_id,
                         const std::string& page_id, std::string& error);
    const ActionItem* SelectedItem() const;

    // Structured action-list search used by the shared Find/Replace sidebar.
    struct ActionSearchResult {
        std::string item_id;
        ActionField field = ActionField::Title;
        int tab_index = 0;
        size_t byte = 0;
        size_t length = 0;
        std::string label;
        std::string preview;
        size_t preview_match_start = 0;
        size_t preview_match_length = 0;
    };
    uint64_t Revision() const;
    std::vector<ActionSearchResult> SearchActions(const std::string& query,
        bool match_case, bool whole_word, size_t limit, bool& truncated);
    void ActivateActionSearchResult(size_t index);
    bool ReplaceActionSearchResult(size_t index,
        const std::string& replacement, std::string& error);
    bool ReplaceAllActionsText(const std::string& query,
        const std::string& replacement, bool match_case, bool whole_word,
        size_t& count, std::string& error);
    void ClearActionSearchHighlights();

private:
    friend class ActionsWorkspaceModalTestAccess;
    enum class Dialog { None, NewGroup, RenameGroup, NewPage, RenamePage, RenameAction,
        DeleteGroup, DeletePage, DeleteAction, MergeGroup };
    const ActionGroup* SelectedGroup() const;
    bool CanDeleteGroup(const ActionGroup& group) const;
    const ActionPage* SelectedPage() const;
    bool CopyActionBlock(const ActionItem& item, size_t block);
    void FlashCopyCaption(const std::string& item_id, size_t block);
    ActionsContextMenu::Result ExecuteContextCommand(
        const ActionsContextMenu::Target& target, ActionsContextMenu::Command command, size_t block);
    void NormalizeSelection();
    void Notify(const std::string& message);
    void OpenNameDialog(Dialog dialog, std::string value = {});
    void AcceptDialog();
    void CloseDialog();
    void AfterMutation(const std::string& status);
    ftxui::Element RenderItems();
    void UpdateItemsVisibleCountFromLayout();
    void ScrollItems(int delta);
    ftxui::Element RenderDialog();
    ftxui::Element RenderItemsScrollbar(size_t visible_rows,
        size_t total_items, size_t visible_items, size_t scroll,
        size_t max_scroll, const Theme& theme,
        const std::shared_ptr<std::vector<int>>& header_rows);
    ftxui::Component ActiveChild() override;

    const Theme* theme_;
    StatusCallback status_;
    ClipboardCallback clipboard_;
    ClipboardController::ReadCallback read_clipboard_;
    GroupsChangedCallback groups_changed_;
    MissingProjectCheck is_missing_project_;
    ActionRepository repository_;
    ActionsContextMenu context_menu_;
    std::optional<std::pair<std::string, std::string>> project_;
    std::string selected_group_id_;
    std::string selected_page_id_;
    std::string selected_item_id_;
    std::string previewed_item_id_;
    size_t previewed_tab_index_ = 0;
    std::string merge_target_id_;
    size_t item_scroll_ = 0;
    Dialog dialog_ = Dialog::None;
    std::optional<ActionsContextMenu::Target> rename_action_target_;
    std::string rename_action_error_;
    std::string name_input_;
    std::shared_ptr<InputSelection> name_selection_;
    ftxui::Component name_component_;
    std::unique_ptr<textlt::ActionEditorModal> action_editor_modal_;
    ftxui::Box items_box_;
    ftxui::Box add_action_box_;
    ftxui::Box items_view_box_;
    ftxui::Box items_scrollbar_box_;
    std::vector<ftxui::Box> item_boxes_;
    std::vector<std::string> item_box_ids_;
    std::vector<std::array<ftxui::Box, kMaxActionTabs>> copy_boxes_;
    std::map<std::pair<std::string, size_t>, std::chrono::steady_clock::time_point>
        copy_caption_deadlines_;
    bool items_view_known_ = false;
    size_t items_visible_rows_ = 0;
    size_t items_visible_count_ = 0;
    std::string items_layout_page_id_;
    std::vector<ActionSearchResult> action_search_matches_;
    size_t action_search_active_ = 0;
    bool items_scrollbar_dragging_ = false;
    int items_scrollbar_drag_y_ = 0;
    size_t items_scrollbar_drag_scroll_ = 0;
};

} // namespace textlt::actions
