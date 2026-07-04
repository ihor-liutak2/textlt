std::vector<std::string> TextProcessorsModalContent::WrapText(
    const std::string& text,
    size_t width) const {
    std::vector<std::string> lines;
    if (text.empty() || width == 0) {
        return lines;
    }

    std::istringstream input(text);
    std::string word;
    std::string line;
    while (input >> word) {
        if (line.empty()) {
            line = word;
            continue;
        }
        if (line.size() + 1 + word.size() <= width) {
            line += " " + word;
            continue;
        }
        lines.push_back(line);
        line = word;
    }

    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

std::string TextProcessorsModalContent::TrimForDisplay(
    const std::string& text,
    size_t max_size) const {
    if (text.size() <= max_size) {
        return text;
    }
    if (max_size <= 3) {
        return text.substr(0, max_size);
    }
    return text.substr(0, max_size - 3) + "...";
}

std::string TextProcessorsModalContent::ScopeLabel(TextParserScope scope) const {
    switch (scope) {
    case TextParserScope::Paragraph:
        return "paragraph";
    case TextParserScope::Code:
        return "code";
    case TextParserScope::Text:
    default:
        return "text";
    }
}

std::string TextProcessorsModalContent::ScopeDisplay(TextParserScope scope) const {
    switch (scope) {
    case TextParserScope::Paragraph:
        return "Paragraph";
    case TextParserScope::Code:
        return "Code";
    case TextParserScope::Text:
    default:
        return "Text";
    }
}

std::vector<std::string> TextProcessorsModalContent::AvailableGroupsForCurrentScope() const {
    std::vector<std::string> groups;
    for (const TextParserDefinition& parser : manager_.GetParsers()) {
        if (parser.scope != active_scope_) {
            continue;
        }
        const std::string group = parser.group.empty() ? "Custom" : parser.group;
        if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
            groups.push_back(group);
        }
    }

    std::vector<std::string> ordered;
    for (const std::string& group : kProcessorGroups) {
        if (group == "All") {
            continue;
        }
        if (std::find(groups.begin(), groups.end(), group) != groups.end()) {
            ordered.push_back(group);
        }
    }
    for (const std::string& group : groups) {
        if (std::find(ordered.begin(), ordered.end(), group) == ordered.end()) {
            ordered.push_back(group);
        }
    }
    return ordered;
}

void TextProcessorsModalContent::EnsureActiveGroupIsAvailable() {
    if (active_group_.empty()) {
        active_group_ = "All";
        return;
    }
    if (active_group_ == "All") {
        return;
    }

    const std::vector<std::string> groups = AvailableGroupsForCurrentScope();
    if (std::find(groups.begin(), groups.end(), active_group_) != groups.end()) {
        return;
    }

    active_group_ = "All";
}

std::string TextProcessorsModalContent::NormalizedParamType(const std::string& type) const {
    const std::string lower = ToLowerCopy(type);
    if (lower == "int" || lower == "integer") {
        return "integer";
    }
    if (lower == "number" || lower == "float" || lower == "double" || lower == "decimal") {
        return "decimal";
    }
    if (lower == "bool" || lower == "boolean") {
        return "boolean";
    }
    return "text";
}

std::string TextProcessorsModalContent::ParamHintText() const {
    const TextParserDefinition* parser = SelectedParser();
    if (!parser) {
        return "";
    }

    for (size_t index = 0; index < parser->params.size() && index < param_inputs_.size(); ++index) {
        if (param_inputs_[index]->Focused() && !parser->params[index].description.empty()) {
            return parser->params[index].description;
        }
    }

    for (const TextParserParam& param : parser->params) {
        if (!param.description.empty()) {
            return param.description;
        }
    }

    return "";
}
