SearchFilesModalContent::SearchFilesModalContent(
    const Theme* theme,
    RootProvider root_provider,
    OpenMatchCallback on_open,
    ReadClipboardCallback read_clipboard,
    WriteClipboardCallback write_clipboard,
    CloseCallback on_close)
    : theme_(theme),
        root_provider_(std::move(root_provider)),
        on_open_(std::move(on_open)),
        read_clipboard_(std::move(read_clipboard)),
        write_clipboard_(std::move(write_clipboard)),
        on_close_(std::move(on_close)) {
    LoadFilters();

    RebuildFilterLabels();
    SyncFilterInputsFromSelection();

    search_tab_button_ = MakeTabButton("Search", 0);
    results_tab_button_ = MakeTabButton("Results", 1);
    filters_tab_button_ = MakeTabButton("Filter Masks", 2);

    tab_buttons_ = ftxui::Container::Horizontal({
        search_tab_button_,
        results_tab_button_,
        filters_tab_button_,
    });

    ftxui::InputOption query_option;
    query_option.multiline = false;
    query_option.on_enter = [this] { ExecuteSearch(); };
    query_option.cursor_position = &query_cursor_position_;
    query_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    query_input_ = ftxui::Input(&query_, "text to search", query_option);
    search_paste_button_ = MakeTextButton("Paste", [this] { PasteSearchQuery(); });
    search_clear_button_ = MakeTextButton("Clear", [this] { ClearSearchQuery(); });

    ftxui::ButtonOption mask_picker_option = ftxui::ButtonOption::Simple();
    mask_picker_option.on_click = [this] {
        selected_filter_ = static_cast<int>(selected_mask_set_);
        SyncFilterInputsFromSelection();
        SelectTab(2);
    };
    mask_picker_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        const auto& mask = mask_sets_[selected_mask_set_];
        ButtonSpec spec = ButtonSpecFromLabel(
            masks_ == mask.value ? TrimForDisplay(mask.name, 42) : "Custom",
            ButtonRole::Navigation);
        return RenderModalFlatButton(theme, spec, state.focused || state.active);
    };
    mask_picker_button_ = ftxui::Button(mask_picker_option);

    ftxui::MenuOption filter_option = ftxui::MenuOption::Vertical();
    filter_option.on_change = [this] {
        SyncFilterInputsFromSelection();
    };
    filter_option.on_enter = [this] { ApplySelectedFilter(); };
    filter_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();

        ftxui::Element row = ftxui::text(TrimForDisplay(state.label, 42)) |
            ftxui::xflex | ftxui::reflect(filter_row_boxes_[static_cast<size_t>(state.index)]);
        if (state.focused || state.active) {
            return row |
            ftxui::bgcolor(theme.modal_selected_item_bg) |
            ftxui::color(theme.modal_selected_item_fg) |
            ftxui::bold;
        }

        return row | ftxui::color(theme.modal_text_color);
    };

    filter_menu_ = ftxui::Menu(&filter_labels_, &selected_filter_, filter_option);
    filter_list_component_ = ftxui::CatchEvent(filter_menu_, [this](ftxui::Event event) {
        return HandleFilterMouseEvent(std::move(event));
    });

    ftxui::InputOption filter_name_option;
    filter_name_option.multiline = false;
    filter_name_option.on_enter = [this] {
        UpdateSelectedFilterFromInputs();
    };
    filter_name_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    filter_name_input_component_ =
    ftxui::Input(&filter_name_input_, "filter name", filter_name_option);

    ftxui::InputOption filter_value_option;
    filter_value_option.multiline = false;
    filter_value_option.on_enter = [this] {
        UpdateSelectedFilterFromInputs();
    };
    filter_value_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    filter_value_input_component_ =
    ftxui::Input(&filter_value_input_, "file masks", filter_value_option);

    context_before_input_component_ = ftxui::Make<ContextInput>(
        context_before_, theme_, [this] { ExecuteSearch(); });
    context_after_input_component_ = ftxui::Make<ContextInput>(
        context_after_, theme_, [this] { ExecuteSearch(); });

    ftxui::MenuOption directory_option = ftxui::MenuOption::Vertical();
    directory_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();

        ftxui::Element row = ftxui::text(state.label);
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }

        return row | ftxui::color(theme.modal_text_color);
    };
    directory_menu_ =
        ftxui::Menu(&directory_labels_, &selected_directory_, directory_option);

        directory_list_component_ = ftxui::CatchEvent(
            directory_menu_,
            [this](ftxui::Event event) {
                if (event == ftxui::Event::Character(" ") &&
                    directory_menu_ &&
                    directory_menu_->Focused()) {
                    ToggleSelectedDirectory();
                return true;
                    }

                    return false;
            });

    toggle_directory_button_ = MakeTextButton("Toggle", [this] { ToggleSelectedDirectory(); });
    all_directories_button_ = MakeTextButton("All", [this] { SelectAllDirectories(); });
    none_directories_button_ = MakeTextButton("None", [this] { ClearDirectorySelection(); });

    tab_footer_buttons_ = {
        MakeFooterButtons({
            MakeTextButton("Search File Contents", [this] { ExecuteSearch(); }),
            MakeTextButton("Search Filenames", [this] { ExecuteSearch(FileSearchTarget::Filename); }),
        }),
        MakeFooterButtons({
            MakeTextButton("Open", [this] { OpenSelectedMatch(); }),
            MakeTextButton("Copy Text", [this] { CopyResultText(); }),
            MakeTextButton("Copy Paths", [this] { CopyResultPaths(); }),
        }),
        MakeFooterButtons({
            MakeTextButton("Use Mask", [this] { ApplySelectedFilter(); }),
            MakeTextButton("Add", [this] { AddFilter(); }),
            MakeTextButton("Delete", [this] { DeleteFilter(); }),
            MakeTextButton("Save", [this] { SaveFilters(); }),
        }),
    };

    ftxui::MenuOption result_option = ftxui::MenuOption::Vertical();
        result_option.entries_option.transform = [this](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();

            ftxui::Element row = ftxui::text(state.label) |
                ftxui::xflex | ftxui::reflect(result_row_boxes_[static_cast<size_t>(state.index)]);
            if (state.focused || state.active) {
                return row |
                    ftxui::bgcolor(theme.modal_selected_item_bg) |
                    ftxui::color(theme.modal_selected_item_fg) |
                    ftxui::bold;
            }

            return row | ftxui::color(theme.modal_foreground);
        };

    result_menu_ = ftxui::Menu(&result_labels_, &selected_result_, result_option);

    result_list_component_ = ftxui::CatchEvent(
        result_menu_,
        [this](ftxui::Event event) {
            if (result_menu_->Focused() && event == ftxui::Event::Return) {
                OpenSelectedMatch();
                return true;
            }

            if (result_menu_->Focused() &&
                (event == ftxui::Event::PageDown || event == ftxui::Event::PageUp)) {
                MoveResultSelection(event == ftxui::Event::PageDown ? 10 : -10);
                return true;
            }

            return HandleResultsMouseEvent(std::move(event));
        });

    search_tab_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            query_input_,
            ftxui::Container::Horizontal({
                search_paste_button_,
                search_clear_button_,
            }),
        }),
        ftxui::Container::Horizontal({
            mask_picker_button_,
            context_before_input_component_,
            context_after_input_component_,
        }),
        ftxui::Container::Horizontal({
            toggle_directory_button_,
            all_directories_button_,
            none_directories_button_,
        }),
        directory_list_component_,
        tab_footer_buttons_[0],
    });

    results_tab_container_ = ftxui::Container::Vertical({
        result_list_component_,
        tab_footer_buttons_[1],
    });

    filters_tab_container_ = ftxui::Container::Vertical({
        filter_list_component_,
        filter_name_input_component_,
        filter_value_input_component_,
        tab_footer_buttons_[2],
    });

    tab_body_container_ = ftxui::Container::Tab({
        search_tab_container_,
        results_tab_container_,
        filters_tab_container_,
    }, &selected_tab_);

    container_ = ftxui::Container::Vertical({
        tab_buttons_,
        tab_body_container_,
    });
}

ftxui::Component SearchFilesModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ButtonSpec spec = SearchButtonSpec(std::move(label));
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(on_click);
    option.transform = [this, spec = std::move(spec)](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RenderModalFlatButton(theme, spec, state.focused || state.active);
    };
    return ftxui::Button(option);
}

ftxui::Component SearchFilesModalContent::MakeFooterButtons(ftxui::Components buttons) {
    buttons.push_back(MakeTextButton("Close", on_close_));
    return ftxui::Container::Horizontal(std::move(buttons));
}

ftxui::Component SearchFilesModalContent::MakeTabButton(
    std::string label,
    int tab_index) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = "  " + label + "  ";
    option.on_click = [this, tab_index] { SelectTab(tab_index); };
    option.transform = [this, tab_index, label = std::move(label)](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        const bool selected = selected_tab_ == tab_index;
        auto tab = RenderModalTabButton(theme, label, selected);
        if (state.focused && !selected) {
            tab |= ftxui::underlined;
        }
        return tab;
    };

    return ftxui::Button(option);
}
