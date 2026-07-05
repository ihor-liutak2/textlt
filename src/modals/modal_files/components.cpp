ftxui::Component FilesModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click,
    ButtonRole role,
    ButtonVariant variant,
    std::string icon,
    ButtonSize size) {
    ButtonSpec spec;
    spec.caption = std::move(label);
    spec.role = role;
    spec.variant = variant;
    spec.size = size;
    spec.icon = std::move(icon);
    return MakeButton(theme_, std::move(spec), std::move(on_click));
}

void FilesModalContent::RebuildComponents() {
    home_button_ = MakeTextButton("Home", [this] {
        LoadBuiltInDirectory(FileManager::UserHomeDirectory());
    }, ButtonRole::Navigation, ButtonVariant::AccentBar, "⌂");
    documents_button_ = MakeTextButton("Documents", [this] {
        LoadBuiltInDirectory(FileManager::UserDocumentsDirectory());
    }, ButtonRole::Navigation, ButtonVariant::AccentBar);
    downloads_button_ = MakeTextButton("Download", [this] {
        LoadBuiltInDirectory(FileManager::UserDownloadsDirectory());
    }, ButtonRole::Navigation, ButtonVariant::AccentBar);
    current_dir_button_ = MakeTextButton("Current Dir", [this] {
        std::filesystem::path directory;
        if (start_directory_provider_) {
            directory = start_directory_provider_();
        }
        LoadBuiltInDirectory(directory);
    }, ButtonRole::Navigation, ButtonVariant::AccentBar);
    add_dir_button_ = MakeTextButton("Add Dir", [this] {
        AddCurrentDirectoryToFavorites();
    }, ButtonRole::Primary, ButtonVariant::AccentBar, "+");
    refresh_button_ = MakeTextButton("Refresh", [this] { Refresh(); },
        ButtonRole::Utility, ButtonVariant::AccentBar, "⟳");
    copy_path_button_ = MakeTextButton("Copy Path", [this] {
        CopySelectedPathText();
    }, ButtonRole::Utility, ButtonVariant::AccentBar, "⧉");

    create_dir_button_ = MakeTextButton("Create Dir", [this] {
        StartCreateDirectoryOperation();
    }, ButtonRole::Primary, ButtonVariant::AccentBar, "+");
    create_file_button_ = MakeTextButton("Create File", [this] {
        StartCreateFileOperation();
    }, ButtonRole::Primary, ButtonVariant::AccentBar, "+");
    delete_button_ = MakeTextButton("Delete", [this] {
        StartDeleteOperation();
    }, ButtonRole::Danger, ButtonVariant::AccentBar, "!");
    rename_button_ = MakeTextButton("Rename", [this] {
        StartRenameOperation();
    }, ButtonRole::Secondary, ButtonVariant::AccentBar);
    copy_button_ = MakeTextButton("Copy", [this] {
        StartCopyOperation();
    }, ButtonRole::Utility, ButtonVariant::AccentBar, "⧉");
    cut_button_ = MakeTextButton("Cut", [this] {
        StartCutOperation();
    }, ButtonRole::Warning, ButtonVariant::AccentBar);
    paste_button_ = MakeTextButton("Paste", [this] {
        StartPasteOperation();
    }, ButtonRole::Utility, ButtonVariant::AccentBar);

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
    }, ButtonRole::Primary, ButtonVariant::AccentBar);
    confirm_cancel_button_ = MakeTextButton("Cancel", [this] {
        CancelPendingOperation();
    }, ButtonRole::Cancel, ButtonVariant::AccentBar);

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
