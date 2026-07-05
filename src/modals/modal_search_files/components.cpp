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
        on_close_(std::move(on_close)),
        mask_sets_(FileSearchEngine::DefaultMaskSets()) {
          LoadFilters();

    if (mask_sets_.empty()) {
        mask_sets_.push_back(FileSearchEngine::DefaultCodeMaskSet());
    }

    RebuildFilterLabels();
    UseMaskSet(0);
    SyncFilterInputsFromSelection();

    search_tab_button_ = MakeTabButton("Search", 0);
    results_tab_button_ = MakeTabButton("Results", 1);
    filters_tab_button_ = MakeTabButton("Filters", 2);

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

    ftxui::InputOption masks_option;
    masks_option.multiline = false;
    masks_option.on_enter = [this] { ExecuteSearch(); };
    masks_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    masks_input_ = ftxui::Input(&masks_, "*.cpp *.hpp *.txt", masks_option);

    ftxui::MenuOption filter_option = ftxui::MenuOption::Vertical();
    filter_option.on_change = [this] {
        SyncFilterInputsFromSelection();
    };
    filter_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();

        ftxui::Element row = ftxui::text(TrimForDisplay(state.label, 42)) |
            ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 43);
        if (state.focused || state.active) {
            return row |
            ftxui::bgcolor(theme.modal_selected_item_bg) |
            ftxui::color(theme.modal_selected_item_fg) |
            ftxui::bold;
        }

        return row | ftxui::color(theme.modal_text_color);
    };

    filter_menu_ = ftxui::Menu(&filter_labels_, &selected_filter_, filter_option);

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

    apply_filter_button_ = MakeTextButton("Apply", [this] { ApplySelectedFilter(); });
    add_filter_button_ = MakeTextButton("Add", [this] { AddFilter(); });
    delete_filter_button_ = MakeTextButton("Delete", [this] { DeleteFilter(); });
    save_filters_button_ = MakeTextButton("Save", [this] { SaveFilters(); });

    ftxui::InputOption context_before_option;
    context_before_option.multiline = false;
    context_before_option.on_enter = [this] { ExecuteSearch(); };
    context_before_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    context_before_input_component_ =
        ftxui::Input(&context_before_input_, "before", context_before_option);

    ftxui::InputOption context_after_option;
    context_after_option.multiline = false;
    context_after_option.on_enter = [this] { ExecuteSearch(); };
    context_after_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    context_after_input_component_ =
        ftxui::Input(&context_after_input_, "after", context_after_option);

    start_mask_button_ = MakeTextButton("<< Start", [this] { UseFirstMaskSet(); });
    previous_mask_button_ = MakeTextButton("< Mask", [this] { UsePreviousMaskSet(); });
    next_mask_button_ = MakeTextButton("Mask >", [this] { UseNextMaskSet(); });

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

    open_button_ = MakeTextButton("Open", [this] { OpenSelectedMatch(); });
    copy_paths_button_ = MakeTextButton("Copy paths", [this] { CopyResultPaths(); });

    ftxui::MenuOption result_option = ftxui::MenuOption::Vertical();
        result_option.entries_option.transform = [this](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();

            ftxui::Element row = ftxui::text(state.label);
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
            if (event == ftxui::Event::Return) {
                OpenSelectedMatch();
                return true;
            }

            return false;
        });

    search_tab_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            query_input_,
            ftxui::Container::Vertical({
                search_paste_button_,
                search_clear_button_,
            }),
        }),
        masks_input_,
        ftxui::Container::Horizontal({
            start_mask_button_,
            previous_mask_button_,
            next_mask_button_,
            context_before_input_component_,
            context_after_input_component_,
        }),
        ftxui::Container::Horizontal({
            toggle_directory_button_,
            all_directories_button_,
            none_directories_button_,
        }),
            directory_list_component_,
    });

    results_tab_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            open_button_,
            copy_paths_button_,
        }),
        result_list_component_,
    });

    filters_tab_container_ = ftxui::Container::Vertical({
        filter_menu_,
        filter_name_input_component_,
        filter_value_input_component_,
        ftxui::Container::Horizontal({
            apply_filter_button_,
            add_filter_button_,
            delete_filter_button_,
            save_filters_button_,
        }),
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
    return MakeButton(theme_, std::move(spec), std::move(on_click));
}

ftxui::Component SearchFilesModalContent::MakeTabButton(
    std::string label,
    int tab_index) {
    ButtonSpec spec;
    spec.caption = std::move(label);
    spec.role = ButtonRole::Tab;
    spec.variant = ButtonVariant::AccentEdges;
    spec.size = ButtonSize::Compact;

    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = [this, tab_index] {
        selected_tab_ = tab_index;
        if (selected_tab_ == 0 && query_input_) {
            query_input_->TakeFocus();
        }
    };
    option.transform = [this, tab_index, spec = std::move(spec)](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ButtonSpec resolved_spec = spec;
        resolved_spec.selected = selected_tab_ == tab_index;
        ftxui::Element tab = RenderButton(theme, resolved_spec, state.focused || state.active);
        if (resolved_spec.selected || state.focused || state.active) {
            tab |= ftxui::bold;
        } else {
            tab |= ftxui::dim;
        }
        return tab;
    };

    return ftxui::Button(option);
}
