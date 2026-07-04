void SearchFilesModalContent::Open() {
    LoadFilters();
    RebuildFilterLabels();
    UseMaskSet(std::min(selected_mask_set_, mask_sets_.size() - 1));
    SyncFilterInputsFromSelection();

    selected_tab_ = 0;
    selected_result_ = 0;
    status_.clear();

    last_clicked_directory_ = -1;
    last_directory_click_time_ = {};

    BuildDirectoryChoices();
    RestoreSelectedDirectoriesForCurrentRoot();
    RefreshDirectoryLabels();

    if (query_input_) {
        query_input_->TakeFocus();
    }
}

void SearchFilesModalContent::Close() {
}

void SearchFilesModalContent::ExecuteSearchFromFooter() {
    ExecuteSearch();
}

ftxui::Element SearchFilesModalContent::RenderTitle() {
    return ftxui::hbox({
        search_tab_button_->Render(),
        ftxui::text(" "),
        results_tab_button_->Render(),
        ftxui::text(" "),
        filters_tab_button_->Render(),
    });
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

    return body |
    ftxui::bgcolor(theme.modal_background) |
    ftxui::color(theme.modal_foreground);
}

std::string SearchFilesModalContent::GetFooterText() const {
    if (!status_.empty()) {
        return status_;
    }

    if (selected_tab_ == 0) {
        return DirectorySummaryText();
    }

    if (selected_tab_ == 1) {
        return StatusText();
    }

    return "Filters are saved to search_file_filter.json";
}
