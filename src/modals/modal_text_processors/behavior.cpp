void TextProcessorsModalContent::Open() {
    Reload();
    if (processor_grid_component_) {
        processor_grid_component_->TakeFocus();
    }
}

void TextProcessorsModalContent::Close() {
    status_ = "Select a text processor.";
    status_is_error_ = false;
    report_text_.clear();
}

void TextProcessorsModalContent::Reload() {
    report_text_.clear();
    std::string error;
    const std::filesystem::path default_processors_directory =
        FindTextProcessorsDirectory();

    if (default_processors_directory.empty()) {
        status_ = "Text processor resources were not found. Reinstall TextLT "
            "or set TEXTLT_DATA_DIR.";
        status_is_error_ = true;
        filtered_parsers_.clear();
        return;
    }

    if (!manager_.EnsureUserConfiguration(default_processors_directory, error)) {
        status_ = error;
        status_is_error_ = true;
        filtered_parsers_.clear();
        return;
    }

    if (!manager_.LoadFromUserConfiguration(error)) {
        status_ = error;
        status_is_error_ = true;
        filtered_parsers_.clear();
        return;
    }

    EnsureActiveGroupIsAvailable();
    RebuildParserList();
    status_ = "Loaded " + std::to_string(manager_.GetParsers().size()) +
        " text processors from user configuration.";
    status_is_error_ = false;
}

void TextProcessorsModalContent::ApplySelected() {
    const TextParserDefinition* parser = SelectedParser();
    if (parser == nullptr) {
        status_ = "No text processor selected.";
        status_is_error_ = true;
        return;
    }

    std::unordered_map<std::string, std::string> params;
    int repeat_count = 1;
    std::string error;
    if (!ValidateCurrentInputs(params, repeat_count, error)) {
        status_ = error;
        status_is_error_ = true;
        return;
    }

    std::string input_text;
    if (!target_text_provider_ ||
        !target_text_provider_(whole_document_, input_text, error)) {
        status_ = error.empty() ? "Cannot read target text." : error;
        status_is_error_ = true;
        return;
    }

    const TextParserApplyResult result = manager_.ApplyParser(
        parser->id,
        input_text,
        params,
        repeat_count);
    if (!result.success) {
        status_ = result.error.empty() ? "Text processor failed." : result.error;
        status_is_error_ = true;
        return;
    }

    if (parser->output == TextParserOutput::Report || result.output == TextParserOutput::Report) {
        report_text_ = result.text;
        status_ = "Analysis completed: " + parser->name + ".";
        status_is_error_ = false;
        return;
    }

    report_text_.clear();
    if (!replace_target_text_ ||
        !replace_target_text_(whole_document_, result.text, error)) {
        status_ = error.empty() ? "Cannot replace target text." : error;
        status_is_error_ = true;
        return;
    }

    status_ = "Applied " + parser->name + " to " +
        (whole_document_ ? "whole document." : "selected text.");
    status_is_error_ = false;

    if (on_close_) {
        on_close_();
    }
}

void TextProcessorsModalContent::TogglePinned() {
    const TextParserDefinition* parser = SelectedParser();
    if (!parser) {
        status_ = "No text processor selected.";
        status_is_error_ = true;
        return;
    }

    const std::string parser_id = parser->id;
    const std::string parser_name = parser->name;
    const bool next_pinned_state = !parser->pinned;

    std::string error;
    if (!manager_.SetParserPinnedInUserConfiguration(
            parser_id,
            next_pinned_state,
            error)) {
        status_ = error.empty() ? "Cannot update pinned state." : error;
        status_is_error_ = true;
        return;
    }

    if (!manager_.LoadFromUserConfiguration(error)) {
        status_ = error.empty() ? "Cannot reload text processors." : error;
        status_is_error_ = true;
        return;
    }

    RebuildParserListKeepingSelection(parser_id);
    status_ = std::string(next_pinned_state ? "Pinned " : "Unpinned ") +
        parser_name + ".";
    status_is_error_ = false;
}

bool TextProcessorsModalContent::HandleEvent(ftxui::Event event) {
    bool input_focused = repeat_input_ && repeat_input_->Focused();
    input_focused = input_focused || (whole_document_checkbox_ && whole_document_checkbox_->Focused());
    for (const auto& input : param_inputs_) {
        input_focused = input_focused || (input && input->Focused());
    }

    if (input_focused && !event.is_mouse()) {
        return false;
    }

    if (event == ftxui::Event::ArrowDown) {
        MoveParserSelection(1);
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        MoveParserSelection(-1);
        return true;
    }

    if (event == ftxui::Event::ArrowRight) {
        MoveParserSelection(0, 1);
        return true;
    }

    if (event == ftxui::Event::ArrowLeft) {
        MoveParserSelection(0, -1);
        return true;
    }

    if (event == ftxui::Event::PageDown) {
        MoveParserSelection(kProcessorListVisibleRows);
        return true;
    }

    if (event == ftxui::Event::PageUp) {
        MoveParserSelection(-kProcessorListVisibleRows);
        return true;
    }

    if (event == ftxui::Event::Return || event.input() == "\x0A") {
        ApplySelected();
        return true;
    }

    if (event.input() == "r" || event.input() == "R") {
        Reload();
        return true;
    }

    if (IsBackspaceEvent(event)) {
        processor_grid_component_->TakeFocus();
        return true;
    }

    if (event.is_mouse()) {
        const ftxui::Mouse& mouse = event.mouse();
        if (mouse.button == ftxui::Mouse::WheelDown) {
            MoveParserSelection(3);
            return true;
        }
        if (mouse.button == ftxui::Mouse::WheelUp) {
            MoveParserSelection(-3);
            return true;
        }
        if (mouse.button == ftxui::Mouse::Left &&
            mouse.motion == ftxui::Mouse::Pressed) {
            for (int index = 0; index < processor_cell_count_; ++index) {
                if (processor_cell_indices_[index] >= 0 &&
                    processor_cell_boxes_[index].Contain(mouse.x, mouse.y)) {
                    MoveParserSelectionToIndex(processor_cell_indices_[index]);
                    processor_grid_component_->TakeFocus();
                    return true;
                }
            }
        }
    }

    return false;
}

void TextProcessorsModalContent::SetScope(TextParserScope scope) {
    if (active_scope_ == scope) {
        return;
    }

    active_scope_ = scope;
    EnsureActiveGroupIsAvailable();
    report_text_.clear();
    selected_parser_ = 0;
    parser_list_top_row_ = 0;
    RebuildParserList();
    status_ = "Showing " + ScopeLabel(scope) + " processors.";
    status_is_error_ = false;
}

TextParserScope TextProcessorsModalContent::CurrentScope() const {
    return active_scope_;
}

void TextProcessorsModalContent::SetGroup(std::string group) {
    if (group.empty()) {
        group = "All";
    }
    if (active_group_ == group) {
        return;
    }
    active_group_ = std::move(group);
    EnsureActiveGroupIsAvailable();
    selected_parser_ = 0;
    parser_list_top_row_ = 0;
    report_text_.clear();
    RebuildParserList();
    status_ = "Showing " + active_group_ + " processors: " +
        std::to_string(filtered_parsers_.size()) + ".";
    status_is_error_ = false;
}

void TextProcessorsModalContent::RebuildParserList() {
    filtered_parsers_.clear();

    for (const TextParserDefinition& parser : manager_.GetParsers()) {
        if (parser.scope != active_scope_) {
            continue;
        }
        if (active_group_ != "All" && parser.group != active_group_) {
            continue;
        }
        filtered_parsers_.push_back(&parser);
    }

    std::stable_sort(filtered_parsers_.begin(), filtered_parsers_.end(),
        [](const TextParserDefinition* left, const TextParserDefinition* right) {
            if (!left || !right) {
                return left != nullptr;
            }
            return left->pinned && !right->pinned;
        });

    selected_parser_ = std::clamp(
        selected_parser_,
        0,
        std::max(0, static_cast<int>(filtered_parsers_.size()) - 1));
    EnsureSelectionVisible();
    SyncParamsFromSelection();
}

void TextProcessorsModalContent::RebuildParserListKeepingSelection(
    const std::string& parser_id) {
    RebuildParserList();
    if (parser_id.empty()) {
        return;
    }

    for (size_t index = 0; index < filtered_parsers_.size(); ++index) {
        const TextParserDefinition* parser = filtered_parsers_[index];
        if (parser && parser->id == parser_id) {
            MoveParserSelectionToIndex(static_cast<int>(index));
            return;
        }
    }
}

void TextProcessorsModalContent::SyncParamsFromSelection() {
    const TextParserDefinition* parser = SelectedParser();
    for (size_t index = 0; index < param_values_.size(); ++index) {
        if (parser && index < parser->params.size()) {
            param_values_[index] = parser->params[index].default_value;
        } else {
            param_values_[index].clear();
        }
        param_cursors_[index] = static_cast<int>(param_values_[index].size());
    }

    if (parser) {
        repeat_count_ = std::to_string(std::max(1, parser->repeat_default));
    } else {
        repeat_count_ = "1";
    }
    repeat_cursor_ = static_cast<int>(repeat_count_.size());
}

const TextParserDefinition* TextProcessorsModalContent::SelectedParser() const {
    if (filtered_parsers_.empty()) {
        return nullptr;
    }
    if (selected_parser_ < 0 ||
        selected_parser_ >= static_cast<int>(filtered_parsers_.size())) {
        return nullptr;
    }
    return filtered_parsers_[static_cast<size_t>(selected_parser_)];
}

bool TextProcessorsModalContent::ValidateCurrentInputs(
    std::unordered_map<std::string, std::string>& params,
    int& repeat_count,
    std::string& error) const {
    params.clear();
    repeat_count = 1;

    const TextParserDefinition* parser = SelectedParser();
    if (!parser) {
        error = "No text processor selected.";
        return false;
    }

    if (!IsIntegerText(repeat_count_)) {
        error = "Repeat must be an integer.";
        return false;
    }

    try {
        repeat_count = std::stoi(repeat_count_);
    } catch (...) {
        error = "Repeat must be an integer.";
        return false;
    }

    if (repeat_count < 1 || repeat_count > 50) {
        error = "Repeat must be between 1 and 50.";
        return false;
    }

    for (size_t index = 0; index < parser->params.size() && index < param_values_.size(); ++index) {
        const TextParserParam& param = parser->params[index];
        const std::string type = NormalizedParamType(param.type);
        std::string value = param_values_[index];

        if (type == "integer") {
            if (!IsIntegerText(value)) {
                error = param.label + " must be an integer.";
                return false;
            }
        } else if (type == "decimal") {
            const char separator = param.decimal_separator.empty()
                ? '.'
                : param.decimal_separator.front();
            if (!IsDecimalText(value, separator)) {
                error = param.label + " must be a decimal number.";
                return false;
            }
            value = NormalizeDecimalText(value, separator);
        } else if (type == "boolean") {
            const std::string lower = ToLowerCopy(value);
            if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
                value = "true";
            } else if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
                value = "false";
            } else {
                error = param.label + " must be true or false.";
                return false;
            }
        }

        params[param.id] = value;
    }

    return true;
}

int TextProcessorsModalContent::RepeatCountOrDefault() const {
    try {
        size_t parsed = 0;
        const int value = std::stoi(repeat_count_, &parsed);
        if (parsed != repeat_count_.size()) {
            return 1;
        }
        return std::clamp(value, 1, 50);
    } catch (...) {
        return 1;
    }
}

void TextProcessorsModalContent::MoveParserSelection(int row_delta, int column_delta) {
    if (filtered_parsers_.empty()) {
        return;
    }

    const int row = selected_parser_ / 2;
    const int column = selected_parser_ % 2;
    int next_row = row + row_delta;
    int next_column = column + column_delta;

    if (next_column < 0) {
        next_column = 0;
    }
    if (next_column > 1) {
        next_column = 1;
    }

    next_row = std::max(0, next_row);
    int next_index = next_row * 2 + next_column;
    if (next_index >= static_cast<int>(filtered_parsers_.size())) {
        next_index = static_cast<int>(filtered_parsers_.size()) - 1;
    }

    MoveParserSelectionToIndex(next_index);
}

void TextProcessorsModalContent::MoveParserSelectionToIndex(int index) {
    if (filtered_parsers_.empty()) {
        selected_parser_ = 0;
        parser_list_top_row_ = 0;
        SyncParamsFromSelection();
        return;
    }

    selected_parser_ = std::clamp(
        index,
        0,
        static_cast<int>(filtered_parsers_.size()) - 1);
    EnsureSelectionVisible();
    SyncParamsFromSelection();
}

void TextProcessorsModalContent::EnsureSelectionVisible() {
    const int selected_row = selected_parser_ / 2;
    const int max_row = std::max(0, (static_cast<int>(filtered_parsers_.size()) + 1) / 2 - 1);
    parser_list_top_row_ = std::clamp(parser_list_top_row_, 0, max_row);

    if (selected_row < parser_list_top_row_) {
        parser_list_top_row_ = selected_row;
    } else if (selected_row >= parser_list_top_row_ + kProcessorListVisibleRows) {
        parser_list_top_row_ = selected_row - kProcessorListVisibleRows + 1;
    }
}
