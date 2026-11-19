void SearchFilesModalContent::Open() {
    LoadFilters();
    RebuildFilterLabels();
    SyncFilterInputsFromSelection();

    selected_result_ = 0;

    last_clicked_directory_ = -1;
    last_directory_click_time_ = {};

    BuildDirectoryChoices();
    RestoreSelectedDirectoriesForCurrentRoot();
    RefreshDirectoryLabels();

    SelectTab(0);
}

void SearchFilesModalContent::Close() {
}

void SearchFilesModalContent::SelectTab(int tab_index) {
    selected_tab_ = tab_index;
    status_.clear();
    last_clicked_filter_ = -1;
    last_clicked_result_ = -1;

    if (tab_index == 0) {
        query_input_->TakeFocus();
    } else if (tab_index == 1 && !summary_.matches.empty()) {
        result_menu_->TakeFocus();
    } else if (tab_index == 2) {
        filter_menu_->TakeFocus();
    } else {
        const auto& footer = tab_footer_buttons_[tab_index];
        footer->ChildAt(footer->ChildCount() - 1)->TakeFocus();
    }
}

ftxui::Element SearchFilesModalContent::RenderTitle() {
    return ftxui::text(GetTitle());
}

ftxui::Element SearchFilesModalContent::RenderHeaderRow() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return hbox({
        search_tab_button_->Render(),
        text(" "),
        results_tab_button_->Render(),
        text(" "),
        filters_tab_button_->Render(),
        filler(),
    }) | bgcolor(theme.modal_background) | color(theme.modal_text_color);
}

ftxui::Element SearchFilesModalContent::Render() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    ftxui::Element body;
    if (selected_tab_ == 0) {
        body = RenderSearchTab();
    } else if (selected_tab_ == 1) {
        body = RenderResultsTab();
    } else {
        body = RenderFiltersTab();
    }

    return ftxui::vbox({
        RenderHeaderRow(),
        ftxui::separator() | ftxui::color(theme.modal_border),
        body | ftxui::flex,
    }) |
    ftxui::bgcolor(theme.modal_background) |
    ftxui::color(theme.modal_foreground);
}

std::string SearchFilesModalContent::FooterSummaryText() const {
    if (selected_tab_ == 0) {
        return DirectorySummaryText();
    }

    if (selected_tab_ == 1) {
        return StatusText();
    }

    return std::to_string(mask_sets_.size()) + " filter masks";
}

ftxui::Element SearchFilesModalContent::RenderCustomFooter() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements buttons = {filler()};
    const auto& footer = tab_footer_buttons_[selected_tab_];
    for (size_t index = 0; index < footer->ChildCount(); ++index) {
        buttons.push_back(text(" "));
        buttons.push_back(footer->ChildAt(index)->Render());
    }

    return vbox({
        text(" " + (status_.empty() ? FooterSummaryText() : status_)) |
            dim | color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        hbox(std::move(buttons)),
    });
}
