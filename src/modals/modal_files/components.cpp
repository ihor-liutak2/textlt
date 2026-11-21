ftxui::Component FilesModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click,
    ButtonRole role,
    ButtonVariant variant,
    std::string icon,
    ButtonSize size) {
    ButtonSpec spec = ButtonSpecFromLabel(
        std::move(label),
        role,
        variant,
        size,
        std::move(icon));
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(on_click);
    option.transform = [this, spec = std::move(spec)](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RenderModalFlatButton(theme, spec, state.focused || state.active);
    };
    return ftxui::Button(option);
}

ftxui::Component FilesModalContent::MakeSelectableInput(
    std::string& value, int& cursor, const std::shared_ptr<InputSelection>& selection,
    std::string placeholder, std::function<void()> on_enter) {
    using namespace ftxui;
    InputOption option;
    option.multiline = false;
    option.on_enter = std::move(on_enter);
    option.transform = [this](InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return state.element | bgcolor(theme.modal_input_bg) | color(theme.modal_input_fg);
    };
    return textlt::MakeSelectableInput(value, cursor, std::move(placeholder),
        std::move(option), selection);
}

void FilesModalContent::RebuildComponents() {
    const std::array<const char*, 4> labels = {"Home", "Documents", "Download", "Current Dir"};
    for (size_t index = 0; index < labels.size(); ++index) {
        ButtonSpec spec = ButtonSpecFromLabel(labels[index], ButtonRole::Navigation);
        if (index == 0) {
            spec.icon = "⌂";
        }
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        option.label = ButtonCaptionText(spec);
        option.on_click = [this, index] { LoadShortcutDirectory(BuiltInDirectories()[index]); };
        option.transform = [this, index, spec = std::move(spec)](const ftxui::EntryState&) mutable {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            spec.selected = index == active_builtin_directory_;
            auto button = RenderModalFlatButton(theme, spec);
            if (!spec.selected && built_in_directory_buttons_[index]->Focused()) {
                button |= ftxui::underlined;
            }
            return button;
        };
        built_in_directory_buttons_[index] = ftxui::Button(option);
    }
    create_dir_button_ = MakeTextButton("Create Dir", [this] {
        StartCreateDirectoryOperation();
    }, ButtonRole::Primary, ButtonVariant::AccentEdges, "+");
    create_file_button_ = MakeTextButton("Create File", [this] {
        StartCreateFileOperation();
    }, ButtonRole::Primary, ButtonVariant::AccentEdges, "+");
    delete_button_ = MakeTextButton("Delete", [this] {
        StartDeleteOperation();
    }, ButtonRole::Danger, ButtonVariant::AccentEdges, "!");
    rename_button_ = MakeTextButton("Rename", [this] {
        StartRenameOperation();
    }, ButtonRole::Navigation, ButtonVariant::AccentEdges);
    copy_button_ = MakeTextButton("Copy", [this] {
        StartCopyOperation();
    }, ButtonRole::Navigation, ButtonVariant::AccentEdges, "⧉");
    cut_button_ = MakeTextButton("Cut", [this] {
        StartCutOperation();
    }, ButtonRole::Warning, ButtonVariant::AccentEdges);
    paste_button_ = MakeTextButton("Paste", [this] {
        StartPasteOperation();
    }, ButtonRole::Navigation, ButtonVariant::AccentEdges);

    path_input_ = MakeSelectableInput(path_input_value_, path_input_cursor_, path_input_selection_,
        "directory path", [this] { LoadPathFromInput(); });
    file_name_input_ = MakeSelectableInput(file_name_input_value_, file_name_input_cursor_,
        file_name_input_selection_, "file name", [this] { ConfirmSelected(); });

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
    }, ButtonRole::Primary, ButtonVariant::AccentEdges);
    confirm_cancel_button_ = MakeTextButton("Cancel", [this] {
        CancelPendingOperation();
    }, ButtonRole::Cancel, ButtonVariant::AccentEdges);

    entry_list_component_ = ftxui::CatchEvent(
        // The bool-renderer overload is focusable. The plain Renderer overload
        // is not, so TakeFocus() in Open() used to leave focus on another
        // control and ArrowUp/ArrowDown never reached file-list navigation.
        ftxui::Renderer([this](bool) { return RenderEntryList(); }),
        [this](ftxui::Event event) { return HandleEvent(event); });

    auto primary_container = ftxui::Container::Vertical({
        ftxui::Container::Horizontal(ftxui::Components(
            built_in_directory_buttons_.begin(), built_in_directory_buttons_.end())),
        path_input_,
        file_name_input_,
        ftxui::Container::Horizontal({
            create_dir_button_,
            create_file_button_,
            rename_button_,
            copy_button_,
            paste_button_,
            delete_button_,
            cut_button_,
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
