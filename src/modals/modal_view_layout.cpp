#include "modal_view_layout.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {

namespace {

std::string LayoutNameForIndex(int index) {
    switch (index) {
        case 1: return "Two columns";
        case 2: return "Three columns";
        default: return "Single column";
    }
}

std::string Shorten(const std::string& value, size_t max_size) {
    if (value.size() <= max_size) {
        return value;
    }
    if (max_size <= 3) {
        return value.substr(0, max_size);
    }
    return value.substr(0, max_size - 3) + "...";
}

} // namespace

ViewLayoutContent::ViewLayoutContent(
    const Theme* theme,
    SnapshotProvider snapshot_provider,
    ApplyLayoutCallback on_apply_layout,
    PaneCallback on_focus_pane,
    PaneDocumentCallback on_assign_document,
    PaneRoleCallback on_set_role,
    ActionCallback on_split_active,
    ActionCallback on_equal_widths)
    : theme_(theme),
      snapshot_provider_(std::move(snapshot_provider)),
      on_apply_layout_(std::move(on_apply_layout)),
      on_focus_pane_(std::move(on_focus_pane)),
      on_assign_document_(std::move(on_assign_document)),
      on_set_role_(std::move(on_set_role)),
      on_split_active_(std::move(on_split_active)),
      on_equal_widths_(std::move(on_equal_widths)) {
    single_button_ = ftxui::Button(MakeButtonOption("Single", [this] {
        selected_layout_index_ = 0;
    }));
    two_button_ = ftxui::Button(MakeButtonOption("Two columns", [this] {
        selected_layout_index_ = 1;
    }));
    three_button_ = ftxui::Button(MakeButtonOption("Three columns", [this] {
        selected_layout_index_ = 2;
    }));

    ftxui::MenuOption pane_option = ftxui::MenuOption::Vertical();
    pane_option.on_change = [this] {
        ClampSelections();
        SyncControlsFromSelectedPane();
    };
    pane_menu_ = ftxui::Menu(&pane_entries_, &selected_pane_index_, pane_option);

    ftxui::MenuOption document_option = ftxui::MenuOption::Vertical();
    document_menu_ = ftxui::Menu(&document_entries_, &selected_document_index_, document_option);

    role_toggle_ = ftxui::Toggle(&role_entries_, &selected_role_index_);

    focus_pane_button_ = ftxui::Button(MakeButtonOption("Set active pane", [this] {
        FocusSelectedPane();
    }));
    assign_document_button_ = ftxui::Button(MakeButtonOption("Assign document", [this] {
        AssignSelectedDocument();
    }));
    set_role_button_ = ftxui::Button(MakeButtonOption("Set role", [this] {
        ApplySelectedRole();
    }));
    split_active_button_ = ftxui::Button(MakeButtonOption("Split active document", [this] {
        if (on_split_active_) {
            on_split_active_();
        }
        RefreshFromApp();
    }));
    equal_widths_button_ = ftxui::Button(MakeButtonOption("Equal widths", [this] {
        if (on_equal_widths_) {
            on_equal_widths_();
        }
        RefreshFromApp();
    }));

    container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            single_button_,
            two_button_,
            three_button_,
        }),
        ftxui::Container::Horizontal({
            pane_menu_,
            document_menu_,
        }),
        role_toggle_,
        ftxui::Container::Horizontal({
            focus_pane_button_,
            assign_document_button_,
            set_role_button_,
        }),
        ftxui::Container::Horizontal({
            split_active_button_,
            equal_widths_button_,
        }),
    });
}

ftxui::ButtonOption ViewLayoutContent::MakeButtonOption(
    const std::string& label,
    std::function<void()> on_click) const {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = label;
    option.on_click = std::move(on_click);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element button = ftxui::text("[" + state.label + "]");
        if (state.focused || state.active) {
            return button |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return button | ftxui::color(theme.modal_accent);
    };
    return option;
}

void ViewLayoutContent::RefreshEntriesFromSnapshot() {
    pane_entries_.clear();
    for (size_t index = 0; index < snapshot_.panes.size(); ++index) {
        const ViewLayoutPaneInfo& pane = snapshot_.panes[index];
        std::string label = "Pane " + std::to_string(index + 1) + ": " +
            Shorten(pane.title.empty() ? "Untitled" : pane.title, 28);
        if (pane.active) {
            label += " [active]";
        }
        pane_entries_.push_back(std::move(label));
    }
    if (pane_entries_.empty()) {
        pane_entries_.push_back("Pane 1: Untitled");
    }

    document_entries_.clear();
    for (size_t index = 0; index < snapshot_.documents.size(); ++index) {
        const ViewLayoutDocumentInfo& doc = snapshot_.documents[index];
        std::string label = std::to_string(index + 1) + ": " +
            Shorten(doc.title.empty() ? "Untitled" : doc.title, 34);
        if (doc.active) {
            label += " [active]";
        }
        document_entries_.push_back(std::move(label));
    }
    if (document_entries_.empty()) {
        document_entries_.push_back("1: Untitled");
    }
}

void ViewLayoutContent::ClampSelections() {
    if (selected_layout_index_ < 0) {
        selected_layout_index_ = 0;
    }
    if (selected_layout_index_ > 2) {
        selected_layout_index_ = 2;
    }
    if (selected_pane_index_ < 0) {
        selected_pane_index_ = 0;
    }
    if (!pane_entries_.empty() && selected_pane_index_ >= static_cast<int>(pane_entries_.size())) {
        selected_pane_index_ = static_cast<int>(pane_entries_.size()) - 1;
    }
    if (selected_document_index_ < 0) {
        selected_document_index_ = 0;
    }
    if (!document_entries_.empty() && selected_document_index_ >= static_cast<int>(document_entries_.size())) {
        selected_document_index_ = static_cast<int>(document_entries_.size()) - 1;
    }
    if (selected_role_index_ < 0) {
        selected_role_index_ = 0;
    }
    if (!role_entries_.empty() && selected_role_index_ >= static_cast<int>(role_entries_.size())) {
        selected_role_index_ = static_cast<int>(role_entries_.size()) - 1;
    }

}

void ViewLayoutContent::SyncControlsFromSelectedPane() {
    ClampSelections();
    if (selected_pane_index_ < 0 ||
        selected_pane_index_ >= static_cast<int>(snapshot_.panes.size())) {
        return;
    }

    const ViewLayoutPaneInfo& pane = snapshot_.panes[static_cast<size_t>(selected_pane_index_)];
    selected_document_index_ = static_cast<int>(pane.document_index);
    const std::string role = pane.role.empty() ? "General" : pane.role;
    auto role_it = std::find(role_entries_.begin(), role_entries_.end(), role);
    if (role_it != role_entries_.end()) {
        selected_role_index_ = static_cast<int>(std::distance(role_entries_.begin(), role_it));
    }
    ClampSelections();
}

void ViewLayoutContent::RefreshFromApp() {
    if (snapshot_provider_) {
        snapshot_ = snapshot_provider_();
        selected_layout_index_ = snapshot_.layout_index;
        selected_pane_index_ = static_cast<int>(snapshot_.active_pane_index);
    } else {
        snapshot_ = ViewLayoutSnapshot{};
    }
    RefreshEntriesFromSnapshot();
    SyncControlsFromSelectedPane();
    ClampSelections();
}

void ViewLayoutContent::ApplySelectedLayout() {
    if (on_apply_layout_) {
        on_apply_layout_(selected_layout_index_);
    }
    RefreshFromApp();
}

void ViewLayoutContent::FocusSelectedPane() {
    ClampSelections();
    if (on_focus_pane_) {
        on_focus_pane_(static_cast<size_t>(selected_pane_index_));
    }
    RefreshFromApp();
}

void ViewLayoutContent::AssignSelectedDocument() {
    ClampSelections();
    if (on_assign_document_) {
        on_assign_document_(
            static_cast<size_t>(selected_pane_index_),
            static_cast<size_t>(selected_document_index_));
    }
    RefreshFromApp();
}

std::string ViewLayoutContent::SelectedRoleLabel() const {
    if (selected_role_index_ < 0 ||
        selected_role_index_ >= static_cast<int>(role_entries_.size())) {
        return "General";
    }
    return role_entries_[static_cast<size_t>(selected_role_index_)];
}

void ViewLayoutContent::ApplySelectedRole() {
    ClampSelections();
    if (on_set_role_) {
        on_set_role_(static_cast<size_t>(selected_pane_index_), SelectedRoleLabel());
    }
    RefreshFromApp();
}

void ViewLayoutContent::TakeFocus() {
    if (container_) {
        container_->TakeFocus();
    }
}

ftxui::Element ViewLayoutContent::RenderLayoutSection(const Theme& theme) {
    using namespace ftxui;

    const std::string split_info = "Splits: 2-left=" + std::to_string(snapshot_.two_left_width) +
        " | 3-left=" + std::to_string(snapshot_.three_left_width) +
        " | 3-right=" + std::to_string(snapshot_.three_right_width);

    return vbox({
        text("Layout") | bold | color(theme.modal_accent),
        hbox({
            single_button_->Render(),
            text(" "),
            two_button_->Render(),
            text(" "),
            three_button_->Render(),
            filler(),
            equal_widths_button_->Render(),
        }),
        text("Selected: " + LayoutNameForIndex(selected_layout_index_) +
             " | Current: " + snapshot_.layout_name) |
            color(theme.modal_text_color),
        text(split_info) | dim | color(theme.modal_text_color),
    });
}

ftxui::Element ViewLayoutContent::RenderPaneManager(const Theme& theme) {
    using namespace ftxui;
    ClampSelections();

    Element selected_details = emptyElement();
    if (selected_pane_index_ >= 0 && selected_pane_index_ < static_cast<int>(snapshot_.panes.size())) {
        const ViewLayoutPaneInfo& pane = snapshot_.panes[static_cast<size_t>(selected_pane_index_)];
        selected_details = vbox({
            text("Selected pane") | bold | color(theme.modal_accent),
            text("Title: " + (pane.title.empty() ? "Untitled" : pane.title)) | color(theme.modal_text_color),
            text("Role: " + (pane.role.empty() ? "General" : pane.role)) | color(theme.modal_text_color),
            text("Document index: " + std::to_string(pane.document_index + 1)) | dim | color(theme.modal_text_color),
        });
    }

    Element pane_list = vbox({
        text("Panes") | bold | color(theme.modal_accent),
        pane_menu_->Render() | frame | size(HEIGHT, LESS_THAN, 7),
        focus_pane_button_->Render(),
    }) | size(WIDTH, GREATER_THAN, 30);

    Element document_list = vbox({
        text("Documents") | bold | color(theme.modal_accent),
        document_menu_->Render() | frame | size(HEIGHT, LESS_THAN, 7),
        assign_document_button_->Render(),
    }) | size(WIDTH, GREATER_THAN, 36) | xflex;

    return vbox({
        hbox({
            pane_list,
            separator() | color(theme.modal_border),
            document_list,
        }),
        separator() | color(theme.modal_border),
        hbox({
            vbox({
                text("Role") | bold | color(theme.modal_accent),
                role_toggle_->Render(),
                set_role_button_->Render(),
            }) | xflex,
            separator() | color(theme.modal_border),
            vbox({
                selected_details,
                split_active_button_->Render(),
            }) | xflex,
        }),
    });
}

ftxui::Element ViewLayoutContent::RenderPanePreview(const Theme& theme) {
    using namespace ftxui;

    Elements rows;
    rows.push_back(text("Preview") | bold | color(theme.modal_accent));
    if (snapshot_.panes.empty()) {
        rows.push_back(text("No panes available") | dim | color(theme.modal_text_color));
        return vbox(std::move(rows));
    }

    for (size_t index = 0; index < snapshot_.panes.size(); ++index) {
        const ViewLayoutPaneInfo& pane = snapshot_.panes[index];
        std::string label = "Pane " + std::to_string(index + 1) + ": ";
        label += pane.title.empty() ? "Untitled" : pane.title;
        label += "  role: " + (pane.role.empty() ? "General" : pane.role);
        if (pane.active) {
            label += "  [active]";
        }
        ftxui::Element row = text(label) | color(theme.modal_text_color);
        if (pane.active) {
            row = row |
                bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg);
        }
        rows.push_back(std::move(row));
    }

    return vbox(std::move(rows));
}

ftxui::Element ViewLayoutContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ClampSelections();

    return vbox({
        RenderLayoutSection(theme),
        separator() | color(theme.modal_border),
        RenderPaneManager(theme) | flex,
        separator() | color(theme.modal_border),
        RenderPanePreview(theme),
        text("Alt+Left / Alt+Right switches panes. Mouse drag on split bars resizes columns.") |
            dim |
            color(theme.modal_text_color),
    }) | bgcolor(theme.modal_input_bg) | color(theme.modal_input_fg);
}

ViewLayoutModal::ViewLayoutModal(
    const Theme* theme,
    SnapshotProvider snapshot_provider,
    ApplyLayoutCallback on_apply_layout,
    PaneCallback on_focus_pane,
    PaneDocumentCallback on_assign_document,
    PaneRoleCallback on_set_role,
    ActionCallback on_split_active,
    ActionCallback on_equal_widths,
    CloseCallback on_close)
    : theme_(theme),
      on_close_(std::move(on_close)) {
    content_ = std::make_shared<ViewLayoutContent>(
        theme_,
        std::move(snapshot_provider),
        std::move(on_apply_layout),
        std::move(on_focus_pane),
        std::move(on_assign_document),
        std::move(on_set_role),
        std::move(on_split_active),
        std::move(on_equal_widths));
    modal_window_ = std::make_shared<ModalWindow>(
        content_,
        theme_,
        [this] { RequestClose(); });
    modal_window_->SetFooterButtons({
        {"Apply layout", [this] {
            if (content_) {
                content_->ApplySelectedLayout();
            }
        }},
        {"Close", [this] { RequestClose(); }},
    });
    modal_window_->SetBodyFrameScrolling(false);
}

ftxui::Component ViewLayoutModal::View() const {
    return modal_window_;
}

void ViewLayoutModal::Open() {
    open_ = true;
    if (content_) {
        content_->RefreshFromApp();
        content_->TakeFocus();
    }
}

void ViewLayoutModal::Close() {
    open_ = false;
}

bool ViewLayoutModal::IsOpen() const {
    return open_;
}

void ViewLayoutModal::TakeFocus() {
    if (content_) {
        content_->TakeFocus();
    }
}

void ViewLayoutModal::SetTheme(const Theme* theme) {
    theme_ = theme;
    if (content_) {
        content_->SetTheme(theme);
    }
    if (modal_window_) {
        modal_window_->SetTheme(theme);
    }
}

void ViewLayoutModal::RequestClose() {
    if (!open_) {
        return;
    }
    open_ = false;
    if (on_close_) {
        on_close_();
    }
}

} // namespace textlt
