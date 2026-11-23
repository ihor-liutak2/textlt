namespace {

std::filesystem::path QuickPanelPath() {
    return TextParserManager::UserConfigDirectory() / "quick_text_commands.json";
}

int QuickDigitIndex(char digit) {
    return digit == '0' ? 9 : digit - '1';
}

} // namespace

void TextProcessorsModalContent::BuildQuickControls() {
    ftxui::MenuOption menu = ftxui::MenuOption::Vertical();
    menu.on_enter = [this] { RunQuick(quick_selected_); };
    menu.entries_option.transform = [this](const ftxui::EntryState& state) {
        using namespace ftxui;
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        Element row = hbox({
            text(" " + std::to_string(state.index + 1)) | size(WIDTH, EQUAL, 8),
            text(state.label), filler(),
        });
        return state.active ? row | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg)
                            : row | color(theme.modal_text_color);
    };
    quick_menu_ = ftxui::Menu(&quick_labels_, &quick_selected_, menu);

    ftxui::InputOption input;
    input.multiline = false;
    input.on_enter = [this] { RemoveQuick(); };
    remove_input_ = ClipboardController::MakeInput(&remove_number_, "Slot number", input);
    quick_controls_ = ftxui::Container::Vertical({
        ftxui::Maybe(quick_menu_, [this] { return !removing_; }),
        ftxui::Maybe(remove_input_, [this] { return removing_; }),
    });
}

bool TextProcessorsModalContent::LoadQuickSlots() {
    const auto path = QuickPanelPath();
    std::error_code error;
    QuickSlots slots{};
    if (std::filesystem::exists(path, error)) {
        const Json saved = LoadJsonValue(path);
        if (!saved.is_array() || saved.size() > slots.size()) {
            status_ = "Cannot read quick text commands.";
            status_is_error_ = true;
            return false;
        }
        try {
            for (size_t i = 0; i < saved.size(); ++i) {
                const auto& item = saved[i];
                if (item.is_null()) continue;
                QuickCommand command;
                item.at("id").get_to(command.id);
                item.at("name").get_to(command.name);
                item.at("params").get_to(command.params);
                item.at("repeat").get_to(command.repeat);
                if (command.id.empty() || command.repeat < 1 || command.repeat > 50) {
                    status_ = "Invalid quick command in slot " + std::to_string(i + 1) + ".";
                    status_is_error_ = true;
                    return false;
                }
                slots[i] = std::move(command);
            }
        } catch (const Json::exception&) {
            status_ = "Invalid quick text command settings.";
            status_is_error_ = true;
            return false;
        }
    } else if (error) {
        status_ = error.message();
        status_is_error_ = true;
        return false;
    } else {
        constexpr std::array default_ids = {
            "builtin_text_cleanup_normalize_spaces",
            "builtin_trim_trailing_spaces",
            "builtin_text_cleanup_join_broken_lines",
            "builtin_punctuation_fix_spaces",
            "builtin_toggle_case",
            "builtin_markdown_bullet_list",
            "builtin_markdown_numbered_list",
            "builtin_sort_lines_az",
            "builtin_unique_lines",
            "builtin_markdown_strip_formatting",
        };
        static_assert(default_ids.size() == std::tuple_size_v<QuickSlots>);
        for (size_t i = 0; i < default_ids.size(); ++i) {
            const auto* parser = manager_.FindParser(default_ids[i]);
            if (!parser) {
                if (!status_is_error_) {
                    status_ = "Cannot initialize quick panel: processor unavailable.";
                    status_is_error_ = true;
                }
                return false;
            }
            QuickCommand command{parser->id, parser->name, {}, parser->repeat_default};
            for (const auto& param : parser->params) {
                command.params.emplace(param.id, param.default_value);
            }
            slots[i] = std::move(command);
        }
        return SaveQuickSlots(slots);
    }
    quick_slots_ = std::move(slots);
    RefreshQuickLabels();
    return true;
}

bool TextProcessorsModalContent::SaveQuickSlots(const QuickSlots& slots) {
    Json saved = Json::array();
    for (const auto& slot : slots) {
        if (slot) {
            saved.push_back({{"id", slot->id}, {"name", slot->name},
                             {"repeat", slot->repeat}, {"params", slot->params}});
        } else {
            saved.push_back(nullptr);
        }
    }
    if (!WriteJsonAtomically(QuickPanelPath(), saved)) {
        status_ = "Cannot save quick text commands.";
        status_is_error_ = true;
        return false;
    }
    quick_slots_ = slots;
    RefreshQuickLabels();
    status_is_error_ = false;
    return true;
}

void TextProcessorsModalContent::RefreshQuickLabels() {
    quick_labels_.clear();
    for (const auto& slot : quick_slots_) {
        if (!slot) {
            quick_labels_.push_back("—");
            continue;
        }
        std::string label = slot->name;
        if (const auto* parser = manager_.FindParser(slot->id)) {
            for (const auto& param : parser->params) {
                const auto saved = slot->params.find(param.id);
                if (saved != slot->params.end()) {
                    label += "  " + param.label + "=" + saved->second;
                }
            }
        }
        if (slot->repeat > 1) label += "  ×" + std::to_string(slot->repeat);
        quick_labels_.push_back(std::move(label));
    }
}

void TextProcessorsModalContent::AddToQuickPanel() {
    const auto* parser = SelectedParser();
    QuickCommand command;
    if (!ValidateCurrentInputs(command.params, command.repeat, status_)) {
        status_is_error_ = true;
        return;
    }
    command.id = parser->id;
    command.name = parser->name;
    if (!LoadQuickSlots()) return;
    QuickSlots slots = quick_slots_;
    auto empty = std::find(slots.begin(), slots.end(), std::nullopt);
    if (empty == slots.end()) {
        status_ = "All ten slots are occupied. Remove a quick command first.";
        status_is_error_ = true;
        return;
    }
    const auto number = std::distance(slots.begin(), empty) + 1;
    *empty = std::move(command);
    if (SaveQuickSlots(slots)) {
        status_ = "Added " + parser->name + " to quick slot " + std::to_string(number) + ".";
    }
}

void TextProcessorsModalContent::OpenQuick(bool use_paragraph) {
    quick_mode_ = true;
    quick_target_ = use_paragraph ? ProcessorTarget::Paragraph : ProcessorTarget::Selection;
    removing_ = false;
    quick_slots_ = {};
    RefreshQuickLabels();
    Reload();
    if (!status_is_error_) {
        status_ = "Press 1–9 or 0 for slot 10. Target: " +
            std::string(use_paragraph ? "current paragraph." : "selected text.");
    }
    if (LoadQuickSlots() && !status_is_error_ &&
        std::none_of(quick_slots_.begin(), quick_slots_.end(), [](const auto& slot) { return slot.has_value(); })) {
        status_ = "Add a command from Text Processors using Add to Quick Panel.";
    }
    quick_menu_->TakeFocus();
}

void TextProcessorsModalContent::RunQuick(int index) {
    quick_selected_ = index;
    const auto& slot = quick_slots_.at(index);
    report_text_.clear();
    if (!slot) {
        status_ = "Empty slot. Use Add to Quick Panel in Text Processors.";
        status_is_error_ = false;
        return;
    }
    const auto* parser = manager_.FindParser(slot->id);
    if (!parser) {
        status_ = "Processor unavailable: " + slot->name;
        status_is_error_ = true;
        return;
    }
    ApplyConfigured(*parser, slot->params, slot->repeat, quick_target_);
}

void TextProcessorsModalContent::PromptRemoveQuick() {
    removing_ = true;
    remove_number_.clear();
    report_text_.clear();
    status_ = "Enter the slot number to remove, then press Enter. Esc cancels.";
    status_is_error_ = false;
    remove_input_->TakeFocus();
}

void TextProcessorsModalContent::RemoveQuick() {
    int index = -1;
    if (remove_number_.empty()) {
        index = quick_selected_;
    } else if (remove_number_ == "10") {
        index = 9;
    } else if (remove_number_.size() == 1 && remove_number_[0] >= '0' && remove_number_[0] <= '9') {
        index = QuickDigitIndex(remove_number_[0]);
    }
    if (index < 0 || !quick_slots_.at(index)) {
        status_ = "Enter an occupied slot number from 1 to 10.";
        status_is_error_ = true;
        return;
    }
    const auto previous = *quick_slots_[index];
    if (!LoadQuickSlots()) return;
    const auto& current = quick_slots_[index];
    if (!current || current->id != previous.id || current->params != previous.params ||
        current->repeat != previous.repeat) {
        status_ = "That slot changed. Check the list and enter the number again.";
        status_is_error_ = true;
        return;
    }
    QuickSlots slots = quick_slots_;
    slots[index].reset();
    if (!SaveQuickSlots(slots)) return;
    removing_ = false;
    quick_selected_ = index;
    quick_menu_->TakeFocus();
    status_ = "Removed quick command from slot " + std::to_string(index + 1) + ".";
}

bool TextProcessorsModalContent::HandleQuickKey(ftxui::Event event) {
    if (!quick_mode_) return false;
    if (removing_ && event == ftxui::Event::Escape) {
        removing_ = false;
        quick_menu_->TakeFocus();
        status_ = "Removal canceled.";
        status_is_error_ = false;
        return true;
    }
    if (!removing_ && event.is_character() && event.input().size() == 1 &&
        event.input()[0] >= '0' && event.input()[0] <= '9') {
        RunQuick(QuickDigitIndex(event.input()[0]));
        return true;
    }
    return false;
}

ftxui::Element TextProcessorsModalContent::RenderQuick() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows = {
        hbox({text(" Slot") | size(WIDTH, EQUAL, 8), text("Command")}) | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        quick_menu_->Render(),
    };
    if (removing_) {
        rows.push_back(hbox({text(" Remove slot (default " + std::to_string(quick_selected_ + 1) + "): "),
                             remove_input_->Render() | xflex}));
    }
    if (!report_text_.empty()) rows.push_back(RenderReportPreview());
    rows.push_back(filler());
    rows.push_back(text(""));
    return vbox(std::move(rows));
}
