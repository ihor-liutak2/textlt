#include "modal_view_layout.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {

namespace {


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
    PaneSessionCallback on_assign_session,
    ActionCallback on_equal_widths,
    std::function<void()> on_close)
    : theme_(theme),
      snapshot_provider_(std::move(snapshot_provider)),
      on_apply_layout_(std::move(on_apply_layout)),
      on_assign_session_(std::move(on_assign_session)),
      on_equal_widths_(std::move(on_equal_widths)),
      on_close_(std::move(on_close)) {
    single_button_ = ftxui::Button(MakeButtonOption("Single", [this] {
        selected_layout_index_ = 0;
        ApplySelectedLayout();
    }, ButtonRole::Toggle));
    two_button_ = ftxui::Button(MakeButtonOption("Two columns", [this] {
        selected_layout_index_ = 1;
        ApplySelectedLayout();
    }, ButtonRole::Toggle));
    three_button_ = ftxui::Button(MakeButtonOption("Three columns", [this] {
        selected_layout_index_ = 2;
        ApplySelectedLayout();
    }, ButtonRole::Toggle));

    ftxui::MenuOption document_option = ftxui::MenuOption::Vertical();
    document_menu_ = ftxui::Menu(&document_entries_, &selected_session_index_, document_option);

    equal_widths_button_ = ftxui::Button(MakeButtonOption("Equal widths", [this] {
        if (on_equal_widths_) {
            on_equal_widths_();
        }
        RefreshFromApp();
    }, ButtonRole::Secondary));
    set_pane_1_button_ = ftxui::Button(MakeButtonOption("Set pane 1", [this] {
        AssignSelectedDocumentToPane(0);
    }, ButtonRole::Primary));
    set_pane_2_button_ = ftxui::Button(MakeButtonOption("Set pane 2", [this] {
        AssignSelectedDocumentToPane(1);
    }, ButtonRole::Primary));
    set_pane_3_button_ = ftxui::Button(MakeButtonOption("Set pane 3", [this] {
        AssignSelectedDocumentToPane(2);
    }, ButtonRole::Primary));
    close_button_ = ftxui::Button(MakeButtonOption("Close", [this] {
        if (on_close_) {
            on_close_();
        }
    }, ButtonRole::Cancel));

    container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            single_button_,
            two_button_,
            three_button_,
            equal_widths_button_,
        }),
        document_menu_,
        ftxui::Container::Horizontal({
            set_pane_1_button_,
            set_pane_2_button_,
            set_pane_3_button_,
            close_button_,
        }),
    });
}

ftxui::ButtonOption ViewLayoutContent::MakeButtonOption(
    std::string label,
    std::function<void()> on_click,
    ButtonRole role,
    std::string icon) const {
    ButtonSpec spec = ButtonSpecFromLabel(std::move(label), role, ButtonVariant::Minimal, ButtonSize::Compact, std::move(icon));
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(on_click);
    option.transform = [this, spec = std::move(spec)](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ButtonSpec resolved_spec = spec;
        if (resolved_spec.caption == "Single") {
            resolved_spec.selected = selected_layout_index_ == 0;
        } else if (resolved_spec.caption == "Two columns") {
            resolved_spec.selected = selected_layout_index_ == 1;
        } else if (resolved_spec.caption == "Three columns") {
            resolved_spec.selected = selected_layout_index_ == 2;
        }
        return RenderModalFlatButton(theme, resolved_spec, state.focused || state.active);
    };
    return option;
}

void ViewLayoutContent::RefreshEntriesFromSnapshot() {
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
    if (selected_session_index_ < 0) {
        selected_session_index_ = 0;
    }
    if (!document_entries_.empty() && selected_session_index_ >= static_cast<int>(document_entries_.size())) {
        selected_session_index_ = static_cast<int>(document_entries_.size()) - 1;
    }
}

void ViewLayoutContent::SyncControlsFromSelectedPane() {
    ClampSelections();
}

void ViewLayoutContent::RefreshFromApp() {
    if (snapshot_provider_) {
        snapshot_ = snapshot_provider_();
        selected_layout_index_ = snapshot_.layout_index;
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

void ViewLayoutContent::AssignSelectedDocumentToPane(size_t pane_index) {
    ClampSelections();
    if (pane_index >= snapshot_.panes.size()) {
        return;
    }
    if (on_assign_session_) {
        on_assign_session_(pane_index, static_cast<size_t>(selected_session_index_));
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

    return vbox({
        hbox({
            single_button_->Render(),
            text("  "),
            two_button_->Render(),
            text("  "),
            three_button_->Render(),
            filler(),
            equal_widths_button_->Render(),
        }),
        separator() | color(theme.modal_border),
    });
}

ftxui::Element ViewLayoutContent::RenderPaneManager(const Theme& theme) {
    using namespace ftxui;
    ClampSelections();

    Elements details;
    if (selected_session_index_ >= 0 &&
        selected_session_index_ < static_cast<int>(snapshot_.documents.size())) {
        const ViewLayoutDocumentInfo& doc = snapshot_.documents[static_cast<size_t>(selected_session_index_)];
        std::string status = doc.dirty ? "modified" : "saved";
        if (doc.memory_only) {
            status += ", memory only";
        }
        details.push_back(text("Selected: " + (doc.title.empty() ? "Untitled" : doc.title)) |
            color(theme.modal_text_color));
        details.push_back(text(status) | dim | color(theme.modal_text_color));
    }

    return vbox({
        document_menu_->Render() |
            frame |
            size(HEIGHT, LESS_THAN, 11) |
            flex,
        separator() | color(theme.modal_border),
        vbox(std::move(details)),
    }) | xflex;
}

ftxui::Element ViewLayoutContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ClampSelections();

    return vbox({
        RenderLayoutSection(theme),
        RenderPaneManager(theme) | flex,
    }) | bgcolor(theme.modal_input_bg) | color(theme.modal_input_fg);
}

ftxui::Element ViewLayoutContent::RenderCustomFooter() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Elements actions;
    const size_t pane_count = std::max<size_t>(snapshot_.panes.size(), 1);
    actions.push_back(set_pane_1_button_->Render());
    if (pane_count >= 2) {
        actions.push_back(text(" "));
        actions.push_back(set_pane_2_button_->Render());
    }
    if (pane_count >= 3) {
        actions.push_back(text(" "));
        actions.push_back(set_pane_3_button_->Render());
    }

    return hbox({
        hbox(std::move(actions)),
        filler(),
        close_button_->Render(),
    }) | bgcolor(theme.modal_background) | color(theme.modal_text_color);
}

ViewLayoutModal::ViewLayoutModal(
    const Theme* theme,
    SnapshotProvider snapshot_provider,
    ApplyLayoutCallback on_apply_layout,
    PaneSessionCallback on_assign_session,
    ActionCallback on_equal_widths,
    CloseCallback on_close)
    : theme_(theme),
      on_close_(std::move(on_close)) {
    content_ = std::make_shared<ViewLayoutContent>(
        theme_,
        std::move(snapshot_provider),
        std::move(on_apply_layout),
        std::move(on_assign_session),
        std::move(on_equal_widths),
        [this] { RequestClose(); });
    modal_window_ = std::make_shared<ModalWindow>(
        content_,
        theme_,
        [this] { RequestClose(); });
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
