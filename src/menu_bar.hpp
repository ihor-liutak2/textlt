#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

namespace textlt {

class MenuBarComponent : public ftxui::ComponentBase {
public:
    using ActionCallback = std::function<void(int menu_index, int item_index)>;

    MenuBarComponent(
        ActionCallback on_action,
        const Theme* theme = nullptr);

    ftxui::Element OnRender() override;
    ftxui::Element RenderDropdown();
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void SetNotesMenuVisible(bool visible);
    void SetActionsMenuVisible(bool visible);
    void SetNotesTrashSelected(bool trash_selected);
    void SetFileFavoriteLabel(bool is_favorite);
    void SetOptionLabels(
        bool smart_word_wrap,
        bool syntax_highlighting,
        bool auto_pairing,
        bool auto_indent,
        int tab_size);
    void CloseDropdown();
    void OpenDropdown(int menu_index);

    bool IsDropdownOpen() const { return active_dropdown_ >= 0; }
    int ActiveDropdown() const { return active_dropdown_; }
    int SelectedDropdownItem() const { return selected_dropdown_item_; }
    std::string CommandIdAt(int menu_index, int item_index) const;

private:
    friend class MenuBarTestAccess;

    struct DropdownItem {
        std::string label;
        std::string command_id;
        bool separator = false;
    };

    void ActivateTopMenu();
    void HandleDropdownAction();
    void RefreshCurrentDropdownEntries();
    void RebuildDropdownComponents();
    bool SetItemLabel(const std::string& command_id, std::string label);
    int MenuIndex(const std::string& label) const;
    bool IsCurrentSeparator(int item_index) const;
    int DropdownX() const;

    std::vector<std::string> menu_entries_;
    std::vector<std::vector<DropdownItem>> dropdown_items_;
    std::vector<DropdownItem> notes_dropdown_items_;
    std::vector<DropdownItem> actions_dropdown_items_;
    std::vector<std::string> current_dropdown_entries_;
    std::vector<std::string> current_dropdown_command_ids_;
    std::vector<bool> current_dropdown_separators_;
    ActionCallback on_action_;
    const Theme* theme_ = nullptr;

    int selected_menu_item_ = 0;
    int active_dropdown_ = -1;
    int selected_dropdown_item_ = 0;
    bool notes_menu_visible_ = true;
    bool actions_menu_visible_ = true;
    bool notes_trash_selected_ = false;

    std::vector<ftxui::Box> menu_boxes_;
    std::vector<ftxui::Box> dropdown_boxes_;
    ftxui::Component top_menu_;
    ftxui::Component dropdown_menu_;
    ftxui::Component menu_container_;
    std::vector<ftxui::Component> dropdown_buttons_;
};

} // namespace textlt
