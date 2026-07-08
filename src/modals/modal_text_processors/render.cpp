ftxui::Element TextProcessorsModalContent::RenderTitle() {
    return ftxui::text(GetTitle());
}

ftxui::Element TextProcessorsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Elements rows;
    rows.push_back(RenderScopeTabs());
    rows.push_back(RenderGroupTabs());
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(hbox({
        RenderSelectedParserInfo() |
            size(WIDTH, EQUAL, 49) |
            flex,
        separator() | color(theme.modal_border),
        RenderParameterFields() |
            size(WIDTH, EQUAL, 50) |
            flex,
    }));
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(vbox({
        text("Processors") | bold | color(theme.modal_accent),
        RenderProcessorGrid() | flex,
    }) | flex);
    if (!report_text_.empty()) {
        rows.push_back(separator() | color(theme.modal_border));
        rows.push_back(RenderReportPreview());
    }
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(RenderStatusLine());

    return vbox(std::move(rows)) | bgcolor(theme.modal_background);
}

ftxui::Element TextProcessorsModalContent::RenderScopeTabs() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        text(" Scope: ") | bold | color(theme.modal_accent),
        active_scope_ == TextParserScope::Text
            ? RenderModalTabButton(theme, "Text", true)
            : text_tab_button_->Render(),
        text(" "),
        active_scope_ == TextParserScope::Paragraph
            ? RenderModalTabButton(theme, "Paragraph", true)
            : paragraph_tab_button_->Render(),
        text(" "),
        active_scope_ == TextParserScope::Code
            ? RenderModalTabButton(theme, "Code", true)
            : code_tab_button_->Render(),
        filler(),
    });
}

ftxui::Element TextProcessorsModalContent::RenderGroupTabs() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::vector<std::string> available_groups = AvailableGroupsForCurrentScope();

    auto group_visible = [&](const std::string& group) {
        return group == "All" ||
            std::find(available_groups.begin(), available_groups.end(), group) != available_groups.end();
    };

    auto render_group_button = [&](size_t index) -> Element {
        const std::string& group = kProcessorGroups[index];
        if (active_group_ == group) {
            return RenderModalTabButton(theme, group, true);
        }
        if (index < group_tab_buttons_.size() && group_tab_buttons_[index]) {
            return group_tab_buttons_[index]->Render();
        }
        return RenderModalTabButton(theme, group, false);
    };

    auto build_row = [&](size_t begin, size_t end, const std::string& label) {
        Elements items;
        items.push_back(text(label) | bold | color(theme.modal_accent));
        bool has_group = false;
        for (size_t index = begin; index < end && index < kProcessorGroups.size(); ++index) {
            const std::string& group = kProcessorGroups[index];
            if (!group_visible(group)) {
                continue;
            }
            if (has_group) {
                items.push_back(text(" "));
            }
            items.push_back(render_group_button(index));
            has_group = true;
        }
        items.push_back(filler());
        return hbox(std::move(items));
    };

    return vbox({
        build_row(0, 6, " Group: "),
        build_row(6, kProcessorGroups.size(), "        "),
    });
}

ftxui::Element TextProcessorsModalContent::RenderSelectedParserInfo() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const TextParserDefinition* parser = SelectedParser();
    if (!parser) {
        return vbox({
            text("Name") | bold | color(theme.modal_accent),
            text("No processor selected.") | color(theme.modal_text_color),
        });
    }

    Elements rows;
    rows.push_back(text("Name") | bold | color(theme.modal_accent));
    rows.push_back(text(parser->name) | color(theme.modal_text_color));
    rows.push_back(hbox({
        text("Engine: ") | bold | color(theme.modal_accent),
        text(parser->engine == TextParserEngine::Builtin ? "Built-in" : "Lua") |
            color(theme.modal_text_color),
        parser->locked
            ? text("  locked") | dim | color(theme.modal_text_color)
            : text("") | color(theme.modal_text_color),
    }));
    rows.push_back(hbox({
        text("Group: ") | bold | color(theme.modal_accent),
        text(parser->group.empty() ? "Custom" : parser->group) | color(theme.modal_text_color),
        text("  Output: ") | bold | color(theme.modal_accent),
        text(parser->output == TextParserOutput::Report ? "Report" : "Replace text") |
            color(theme.modal_text_color),
    }));
    rows.push_back(text(""));
    rows.push_back(text("Description") | bold | color(theme.modal_accent));

    const std::vector<std::string> description_lines = WrapText(parser->description, 45);
    if (description_lines.empty()) {
        rows.push_back(text("No description.") | dim | color(theme.modal_text_color));
    } else {
        for (const std::string& line : description_lines) {
            rows.push_back(text(line) | color(theme.modal_text_color));
        }
    }

    return vbox(std::move(rows));
}

ftxui::Element TextProcessorsModalContent::RenderParameterFields() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const TextParserDefinition* parser = SelectedParser();
    Elements rows;

    rows.push_back(hbox({
        text("Parameters") | bold | color(theme.modal_accent),
        filler(),
        text("Repeat: ") | color(theme.modal_text_color),
        repeat_input_->Render() | size(WIDTH, EQUAL, 5),
    }));
    rows.push_back(whole_document_checkbox_->Render());

    if (!parser || parser->params.empty()) {
        rows.push_back(text("No parameters for this processor.") |
            dim |
            color(theme.modal_text_color));
        return vbox(rows);
    }

    for (size_t index = 0; index < parser->params.size() && index < param_inputs_.size(); ++index) {
        const TextParserParam& param = parser->params[index];
        const std::string type = NormalizedParamType(param.type);
        rows.push_back(hbox({
            text(TrimForDisplay(param.label, 17) + ": ") |
                color(theme.modal_text_color) |
                size(WIDTH, EQUAL, 19),
            param_inputs_[index]->Render() |
                size(WIDTH, GREATER_THAN, 18) |
                xflex,
            text(" ") | color(theme.modal_text_color),
            text(type) |
                dim |
                color(theme.modal_text_color) |
                size(WIDTH, EQUAL, 8),
        }));
    }

    const std::string hint = ParamHintText();
    if (!hint.empty()) {
        rows.push_back(text(""));
        rows.push_back(text("Hint") | bold | color(theme.modal_accent));
        for (const std::string& line : WrapText(hint, 46)) {
            rows.push_back(text(line) | dim | color(theme.modal_text_color));
        }
    }

    return vbox(rows);
}

ftxui::Element TextProcessorsModalContent::RenderProcessorGrid() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (filtered_parsers_.empty()) {
        processor_cell_count_ = 0;
        return text("No processors for " + ScopeLabel(active_scope_) + ".") |
            color(theme.modal_text_color) |
            frame;
    }

    const int visible_rows = kProcessorListVisibleRows;
    processor_cell_count_ = 0;

    return hbox({
        RenderProcessorColumn(false, visible_rows) |
            size(WIDTH, EQUAL, 49) |
            flex,
        separator() | color(theme.modal_border),
        RenderProcessorColumn(true, visible_rows) |
            size(WIDTH, EQUAL, 50) |
            flex,
    }) |
        vscroll_indicator |
        size(HEIGHT, EQUAL, kProcessorListVisibleRows);
}

ftxui::Element TextProcessorsModalContent::RenderProcessorColumn(
    bool right_column,
    int visible_rows) const {
    using namespace ftxui;

    Elements rows;
    for (int row = parser_list_top_row_; row < parser_list_top_row_ + visible_rows; ++row) {
        const int parser_index = row * 2 + (right_column ? 1 : 0);
        const int slot = processor_cell_count_ < kMaxVisibleProcessorCells
            ? processor_cell_count_++
            : kMaxVisibleProcessorCells - 1;
        rows.push_back(RenderProcessorCell(parser_index, right_column ? 50 : 49, slot));
    }

    return vbox(std::move(rows)) | flex;
}

ftxui::Element TextProcessorsModalContent::RenderProcessorCell(
    int parser_index,
    int width,
    int cell_slot) const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (cell_slot >= 0 && cell_slot < kMaxVisibleProcessorCells) {
        processor_cell_indices_[cell_slot] = parser_index;
    }

    if (parser_index < 0 || parser_index >= static_cast<int>(filtered_parsers_.size())) {
        if (cell_slot >= 0 && cell_slot < kMaxVisibleProcessorCells) {
            processor_cell_indices_[cell_slot] = -1;
        }
        return text(" ") |
            size(WIDTH, EQUAL, width) |
            reflect(processor_cell_boxes_[cell_slot]);
    }

    const TextParserDefinition* parser = filtered_parsers_[static_cast<size_t>(parser_index)];
    std::string label = parser ? parser->name : "";
    if (label.empty() && parser) {
        label = parser->script_path.filename().string();
    }
    const std::string engine_marker =
        parser && parser->engine == TextParserEngine::Builtin ? "[B] " : "[L] ";
    if (parser && parser->pinned) {
        label = "* " + engine_marker + label;
    } else {
        label = "  " + engine_marker + label;
    }

    ftxui::Element row = text(" " + TrimForDisplay(label, static_cast<size_t>(width - 2)) + " ") |
        size(WIDTH, EQUAL, width) |
        reflect(processor_cell_boxes_[cell_slot]);

    if (parser_index == selected_parser_) {
        return row |
            bgcolor(theme.modal_selected_item_bg) |
            color(theme.modal_selected_item_fg) |
            bold;
    }

    if (parser && parser->pinned) {
        return row | color(theme.modal_accent);
    }

    return row | color(theme.modal_text_color);
}

ftxui::Element TextProcessorsModalContent::RenderStatusLine() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string prefix = status_is_error_ ? "Error: " : "Status: ";
    return hbox({
        text(" " + prefix + TrimForDisplay(status_, 100) + " ") |
            color(status_is_error_ ? theme.modal_accent : theme.modal_text_color),
        filler(),
    });
}

ftxui::Element TextProcessorsModalContent::RenderReportPreview() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows;
    rows.push_back(text(" Analysis report ") | bold | color(theme.modal_accent));

    std::istringstream input(report_text_);
    std::string line;
    int shown = 0;
    while (shown < 5 && std::getline(input, line)) {
        rows.push_back(text(" " + TrimForDisplay(line, 100) + " ") | color(theme.modal_text_color));
        ++shown;
    }

    if (input.good()) {
        rows.push_back(text(" ...") | dim | color(theme.modal_text_color));
    }

    return vbox(std::move(rows));
}
