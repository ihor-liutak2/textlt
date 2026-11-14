#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <memory>
#include <string>
#include <vector>

#include "actions/action_repository.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"
#include "modals/modal_action_editor.hpp"

namespace textlt::actions {

class ActionsWorkspaceComponent : public ftxui::ComponentBase {
public:
    using StatusCallback = std::function<void(const std::string&)>;
    using ClipboardCallback = std::function<bool(const std::string&)>;
    using GroupsChangedCallback = std::function<void()>;

    ActionsWorkspaceComponent(const Theme* theme, StatusCallback status,
        ClipboardCallback clipboard, GroupsChangedCallback groups_changed,
        std::filesystem::path storage_path = {});
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }
    void Open();
    bool Save(std::string& error) const;
    void SetProjectContext(std::optional<std::pair<std::string, std::string>> project);

    std::vector<ActionGroup> Groups() const { return repository_.Data().groups; }
    std::string SelectedGroupId() const { return selected_group_id_; }
    void SelectGroup(const std::string& id);

    void NewGroup();
    void RenameGroup();
    void DeleteGroup();
    void NewPage();
    void RenamePage();
    void DeletePage();
    void MovePage(int delta);
    void AddAction();
    void EditAction();
    void DeleteAction();
    void MoveAction(int delta);
    void CopySelectedAction();
    void CopyAllActions();
    bool HasSelectedPage() const;
    bool ImportActions(std::vector<ActionItem> items, std::string& error);

private:
    friend class ActionsWorkspaceModalTestAccess;
    enum class FocusArea { Pages, Items };
    enum class Dialog { None, NewGroup, RenameGroup, NewPage, RenamePage,
        DeleteGroup, DeletePage, DeleteAction };
    const ActionGroup* SelectedGroup() const;
    const ActionPage* SelectedPage() const;
    const ActionItem* SelectedItem() const;
    void NormalizeSelection();
    void Notify(const std::string& message);
    void OpenNameDialog(Dialog dialog, std::string value = {});
    void AcceptDialog();
    void CloseDialog();
    void AfterMutation(const std::string& status);
    ftxui::Element RenderPages();
    ftxui::Element RenderItems();
    ftxui::Element RenderItemsScrollbar(size_t visible_rows, size_t total_items,
        size_t visible_items, size_t scroll, size_t max_scroll, const Theme& theme);
    void ScrollItems(int delta);
    ftxui::Element RenderDialog();
    ftxui::Component ActiveChild() override;

    const Theme* theme_;
    StatusCallback status_;
    ClipboardCallback clipboard_;
    GroupsChangedCallback groups_changed_;
    ActionRepository repository_;
    std::optional<std::pair<std::string, std::string>> project_;
    std::string selected_group_id_;
    std::string selected_page_id_;
    std::string selected_item_id_;
    FocusArea focus_ = FocusArea::Pages;
    size_t page_scroll_ = 0;
    size_t item_scroll_ = 0;
    Dialog dialog_ = Dialog::None;
    std::string name_input_;
    int name_cursor_ = 0;
    ftxui::Component name_component_;
    std::unique_ptr<textlt::ActionEditorModal> action_editor_modal_;
    ftxui::Box pages_box_;
    ftxui::Box items_box_;
    ftxui::Box add_action_box_;
    ftxui::Box pages_view_box_;
    ftxui::Box items_view_box_;
    ftxui::Box items_scrollbar_box_;
    std::vector<ftxui::Box> page_boxes_;
    std::vector<ftxui::Box> item_boxes_;
    std::vector<ftxui::Box> copy_boxes_;
    bool pages_view_known_ = false;
    bool items_view_known_ = false;
    size_t pages_visible_count_ = 0;
    size_t items_visible_rows_ = 0;
    size_t items_visible_count_ = 0;
    bool items_scrollbar_dragging_ = false;
    int items_scrollbar_drag_y_ = 0;
    size_t items_scrollbar_drag_scroll_ = 0;
};

} // namespace textlt::actions
