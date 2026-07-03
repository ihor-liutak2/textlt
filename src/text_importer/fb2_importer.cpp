/* Included by ../text_importer.cpp. */

std::string CollectFb2InlineText(const pugi::xml_node& node) {
    std::string text;

    for (pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            text += child.value();
            continue;
        }

        if (child.type() != pugi::node_element) {
            continue;
        }

        if (IsAnyNode(child, {"br", "empty-line"})) {
            text.push_back('\n');
            continue;
        }
        if (IsNode(child, "image")) {
            continue;
        }

        text += CollectFb2InlineText(child);
    }

    return text;
}

void AppendFb2InlineBlock(const pugi::xml_node& node, std::string& output) {
    const std::string text = CollectFb2InlineText(node);
    for (const std::string& line : SplitLines(text)) {
        AppendNonEmptyNormalizedLine(output, line);
    }
}

std::string ExtractFb2TableCellText(const pugi::xml_node& cell) {
    return NormalizeInlineWhitespace(CollectFb2InlineText(cell));
}

void AppendFb2Table(const pugi::xml_node& table, std::string& output) {
    for (pugi::xml_node row : table.children()) {
        if (!IsNode(row, "tr")) {
            continue;
        }

        std::vector<std::string> cells;
        for (pugi::xml_node cell : row.children()) {
            if (IsAnyNode(cell, {"td", "th"})) {
                cells.push_back(ExtractFb2TableCellText(cell));
            }
        }

        const std::string row_text = JoinCells(cells);
        if (!row_text.empty()) {
            AppendLine(output, row_text);
        }
    }
}

void AppendFb2Title(const pugi::xml_node& title, std::string& output) {
    bool wrote_line = false;
    for (pugi::xml_node child : title.children()) {
        if (IsAnyNode(child, {"p", "subtitle", "v"})) {
            const std::string before = output;
            AppendFb2InlineBlock(child, output);
            if (output != before) {
                wrote_line = true;
            }
        }
    }

    if (!wrote_line) {
        const std::string text = NormalizeInlineWhitespace(CollectFb2InlineText(title));
        if (!text.empty()) {
            AppendLine(output, text);
            wrote_line = true;
        }
    }

    if (wrote_line) {
        AppendBlankLine(output);
    }
}

void AppendFb2Node(const pugi::xml_node& node, std::string& output) {
    for (pugi::xml_node child : node.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }

        const std::string local_name = LocalName(child);
        if (local_name == "title") {
            AppendFb2Title(child, output);
        } else if (local_name == "subtitle" || local_name == "p" || local_name == "v") {
            AppendFb2InlineBlock(child, output);
        } else if (local_name == "empty-line") {
            AppendBlankLine(output);
        } else if (local_name == "table") {
            AppendFb2Table(child, output);
            AppendBlankLine(output);
        } else if (IsAnyNode(child, {"section", "body", "poem", "stanza", "cite", "epigraph", "annotation"})) {
            AppendFb2Node(child, output);
            if (local_name == "section") {
                AppendBlankLine(output);
            }
        } else if (local_name == "image" || local_name == "binary") {
            continue;
        } else {
            AppendFb2Node(child, output);
        }
    }
}



std::string ParseFb2Xml(const std::string& xml) {
    const std::string normalized_xml = NormalizeXmlInputEncoding(xml);

    pugi::xml_document document;
    const pugi::xml_parse_result parse_result = document.load_buffer(
        normalized_xml.data(),
        normalized_xml.size(),
        pugi::parse_default | pugi::parse_ws_pcdata_single,
        pugi::encoding_auto);
    if (!parse_result) {
        throw std::runtime_error(std::string("Unable to parse FB2 XML: ") + parse_result.description());
    }

    pugi::xml_node root = document.document_element();
    if (!root) {
        throw std::runtime_error("Unable to parse FB2 XML: document root was not found.");
    }

    std::string output;
    for (pugi::xml_node child : root.children()) {
        if (child.type() == pugi::node_element && IsNode(child, "body")) {
            AppendFb2Node(child, output);
            AppendBlankLine(output);
        }
    }

    if (output.empty()) {
        pugi::xml_node body = FindFirstDescendantByLocalName(root, "body");
        if (body) {
            AppendFb2Node(body, output);
        }
    }

    return TrimTrailingNewlines(output);
}
