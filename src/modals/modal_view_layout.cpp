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

} // namespace

ViewLayoutContent::ViewLayoutContent(
    const Theme* theme,
    SnapshotProvider snapshot_provider,
    ApplyCallback on_apply)
    : theme_(theme),
      snapshot_provider_(std::move(snapshot_provider)),
      on_apply_(std::move(on_apply)) {
    single_button_ = ftxui::Button(MakeButtonOption("Single", [this] {
        selected_layout_index_ = 0;
    }));
    two_button_ = ftxui::Button(MakeButtonOption("Two columns", [this] {
        selected_layout_index_ = 1;
    }));
    three_button_ = ftxui::Button(MakeButtonOption("Three columns", [this] {
        selected_layout_index_ = 2;
    }));
    container_ = ftxui::Container::Horizontal({
        single_button_,
        two_button_,
        three_button_,
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

void ViewLayoutContent::RefreshFromApp() {
    if (snapshot_provider_) {
        snapshot_ = snapshot_provider_();
        selected_layout_index_ = snapshot_.layout_index;
    } else {
        snapshot_ = ViewLayoutSnapshot{};
    }
}

void ViewLayoutContent::ApplySelectedLayout() {
    if (on_apply_) {
        on_apply_(selected_layout_index_);
    }
    RefreshFromApp();
}

void ViewLayoutContent::TakeFocus() {
    if (container_) {
        container_->TakeFocus();
    }
}

ftxui::Element ViewLayoutContent::RenderLayoutButtons(const Theme& theme) {
    using namespace ftxui;

    return vbox({
        text("Layout") | bold | color(theme.modal_accent),
        hbox({
            single_button_->Render(),
            text(" "),
            two_button_->Render(),
            text(" "),
            three_button_->Render(),
        }),
        text("Selected: " + LayoutNameForIndex(selected_layout_index_)) |
            color(theme.modal_text_color),
    });
}

ftxui::Element ViewLayoutContent::RenderPanePreview(const Theme& theme) {
    using namespace ftxui;

    Elements rows;
    rows.push_back(text("Panes") | bold | color(theme.modal_accent));
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

    return vbox({
        RenderLayoutButtons(theme),
        separator() | color(theme.modal_border),
        text("Current: " + snapshot_.layout_name) | color(theme.modal_text_color),
        text("Mouse and keyboard events are routed by FTXUI containers. The active pane receives editor commands.") |
            dim |
            color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        RenderPanePreview(theme),
    }) | bgcolor(theme.modal_input_bg) | color(theme.modal_input_fg);
}

ViewLayoutModal::ViewLayoutModal(
    const Theme* theme,
    SnapshotProvider snapshot_provider,
    ApplyCallback on_apply,
    CloseCallback on_close)
    : theme_(theme),
      on_close_(std::move(on_close)) {
    content_ = std::make_shared<ViewLayoutContent>(
        theme_,
        std::move(snapshot_provider),
        std::move(on_apply));
    modal_window_ = std::make_shared<ModalWindow>(
        content_,
        theme_,
        [this] { RequestClose(); });
    modal_window_->SetFooterButtons({
        {"Apply", [this] {
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
