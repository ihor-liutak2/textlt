ftxui::Component FilesModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = std::move(on_click);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element button = ftxui::text(BracketLabel(state.label));
        if (state.focused || state.active) {
            return button |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return button | ftxui::color(theme.modal_accent);
    };
    return ftxui::Button(option);
}

void FilesModalContent::RebuildComponents() {
    home_button_ = MakeTextButton("Home", [this] {
        LoadBuiltInDirectory(FileManager::UserHomeDirectory());
    });
    documents_button_ = MakeTextButton("Documents", [this] {
        LoadBuiltInDirectory(FileManager::UserDocumentsDirectory());
    });
    downloads_button_ = MakeTextButton("Download", [this] {
        LoadBuiltInDirectory(FileManager::UserDownloadsDirectory());
    });
    current_dir_button_ = MakeTextButton("Current Dir", [this] {
        std::filesystem::path directory;
        if (start_directory_provider_) {
            directory = start_directory_provider_();
        }
        LoadBuiltInDirectory(directory);
    });
    add_dir_button_ = MakeTextButton("Add Dir", [this] {
        AddCurrentDirectoryToFavorites();
    });
    refresh_button_ = MakeTextButton("Refresh", [this] { Refresh(); });
    copy_path_button_ = MakeTextButton("Copy Path", [this] {
        CopySelectedPathText();
    });

    create_dir_button_ = MakeTextButton("Create Dir", [this] {
        StartCreateDirectoryOperation();
    });
    create_file_button_ = MakeTextButton("Create File", [this] {
        StartCreateFileOperation();
    });
    delete_button_ = MakeTextButton("Delete", [this] {
        StartDeleteOperation();
    });
    rename_button_ = MakeTextButton("Rename", [this] {
        StartRenameOperation();
    });
    copy_button_ = MakeTextButton("Copy", [this] {
        StartCopyOperation();
    });
    cut_button_ = MakeTextButton("Cut", [this] {
        StartCutOperation();
    });
    paste_button_ = MakeTextButton("Paste", [this] {
        StartPasteOperation();
    });

    ftxui::InputOption path_option;
    path_option.multiline = false;
    path_option.cursor_position = &path_input_cursor_;
    path_option.on_enter = [this] { LoadPathFromInput(); };
    path_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return state.element |
            ftxui::bgcolor(theme.modal_input_bg) |
            ftxui::color(theme.modal_input_fg);
    };
    path_input_ = ftxui::Input(&path_input_value_, "directory path", path_option);

    ftxui::InputOption file_option = path_option;
    file_option.cursor_position = &file_name_input_cursor_;
    file_option.on_enter = [this] { ConfirmSelected(); };
    file_name_input_ = ftxui::Input(&file_name_input_value_, "file name", file_option);

    ftxui::InputOption operation_option;
    operation_option.multiline = false;
    operation_option.cursor_position = &pending_operation_input_cursor_;
    operation_option.on_enter = [this] { ConfirmPendingOperation(); };
    operation_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return state.element |
            ftxui::bgcolor(theme.modal_input_bg) |
            ftxui::color(theme.modal_input_fg);
    };
    operation_input_ = ftxui::Input(
        &pending_operation_input_value_,
        "name",
        operation_option);
    confirm_yes_button_ = MakeTextButton("Confirm", [this] {
        ConfirmPendingOperation();
    });
    confirm_cancel_button_ = MakeTextButton("Cancel", [this] {
        CancelPendingOperation();
    });

    entry_list_component_ = ftxui::CatchEvent(
        ftxui::Renderer([this] { return RenderEntryList(); }),
        [this](ftxui::Event event) { return HandleEvent(event); });

    auto primary_container = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            home_button_,
            documents_button_,
            downloads_button_,
            current_dir_button_,
            add_dir_button_,
            refresh_button_,
            copy_path_button_,
        }),
        path_input_,
        file_name_input_,
        ftxui::Container::Horizontal({
            create_dir_button_,
            create_file_button_,
            delete_button_,
            rename_button_,
            copy_button_,
            cut_button_,
            paste_button_,
        }),
        entry_list_component_,
    });
    operation_container_ = ftxui::CatchEvent(
        ftxui::Container::Vertical({
            operation_input_,
            ftxui::Container::Horizontal({
                confirm_yes_button_,
                confirm_cancel_button_,
            }),
        }),
        [this](ftxui::Event event) {
            if (IsEscapeEvent(event)) {
                CancelPendingOperation();
                SetStatus("Operation canceled.");
                return true;
            }
            if ((event == ftxui::Event::Return || event.input() == "\x0A") &&
                !PendingOperationNeedsInput()) {
                ConfirmPendingOperation();
                return true;
            }
            return false;
        });
    container_ = ftxui::Container::Tab(
        {primary_container, operation_container_}, &operation_layer_index_);
}
