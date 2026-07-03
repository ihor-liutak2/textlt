/* Included by ../text_importer.cpp. */

std::string ExtractOdtInlineText(const pugi::xml_node& node) {
    std::string text;

    for (pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            text += child.value();
            continue;
        }
        if (child.type() != pugi::node_element) {
            continue;
        }

        const std::string local_name = LocalName(child);
        if (local_name == "line-break") {
            text.push_back('\n');
        } else if (local_name == "tab") {
            text.push_back('\t');
        } else if (local_name == "s") {
            const int count = AttributeIntByLocalName(child, "c", 1);
            text.append(static_cast<size_t>(count), ' ');
        } else if (local_name == "soft-page-break") {
            // Page breaks are layout hints and should not appear in imported plain text.
        } else if (local_name == "note" || local_name == "annotation") {
            // Keep the main document readable. Notes can be imported later as metadata if needed.
        } else {
            text += ExtractOdtInlineText(child);
        }
    }

    return text;
}

std::string ExtractOdtCellText(const pugi::xml_node& cell);

void AppendOdtTable(const pugi::xml_node& table, std::string& output) {
    for (pugi::xml_node row : table.children()) {
        if (!IsNode(row, "table-row")) {
            continue;
        }

        std::vector<std::string> cells;
        for (pugi::xml_node cell : row.children()) {
            if (!IsAnyNode(cell, {"table-cell", "covered-table-cell"})) {
                continue;
            }
            const int repeat = AttributeIntByLocalName(cell, "number-columns-repeated", 1);
            const std::string cell_text = ExtractOdtCellText(cell);
            for (int i = 0; i < repeat; ++i) {
                cells.push_back(cell_text);
            }
        }

        const std::string row_text = JoinCells(cells);
        if (!row_text.empty()) {
            AppendLine(output, row_text);
        }
    }
}

void AppendOdtBlocks(const pugi::xml_node& container, std::string& output) {
    for (pugi::xml_node child : container.children()) {
        if (child.type() != pugi::node_element) {
            continue;
        }

        const std::string local_name = LocalName(child);
        if (local_name == "p" || local_name == "h") {
            const std::string paragraph = NormalizeInlineWhitespace(ExtractOdtInlineText(child));
            if (!paragraph.empty()) {
                AppendLine(output, paragraph);
            } else {
                AppendBlankLine(output);
            }
        } else if (local_name == "table") {
            AppendOdtTable(child, output);
            AppendBlankLine(output);
        } else if (local_name == "list-item") {
            std::string item_text;
            AppendOdtBlocks(child, item_text);
            item_text = NormalizeImportedPlainText(item_text);
            if (!item_text.empty()) {
                AppendLine(output, "- " + item_text);
            }
        } else if (IsAnyNode(child, {"text", "body", "section", "list", "span", "a", "tracked-changes"})) {
            AppendOdtBlocks(child, output);
        } else {
            AppendOdtBlocks(child, output);
        }
    }
}

std::string ExtractOdtCellText(const pugi::xml_node& cell) {
    std::string text;
    for (pugi::xml_node child : cell.children()) {
        if (IsNode(child, "p") || IsNode(child, "h")) {
            const std::string paragraph = NormalizeInlineWhitespace(ExtractOdtInlineText(child));
            if (!paragraph.empty()) {
                if (!text.empty()) {
                    text.push_back(' ');
                }
                text += paragraph;
            }
        } else if (IsNode(child, "table")) {
            std::string nested_table;
            AppendOdtTable(child, nested_table);
            nested_table = NormalizeInlineWhitespace(nested_table);
            if (!nested_table.empty()) {
                if (!text.empty()) {
                    text.push_back(' ');
                }
                text += nested_table;
            }
        } else if (child.type() == pugi::node_element) {
            std::string nested;
            AppendOdtBlocks(child, nested);
            nested = NormalizeImportedPlainText(nested);
            if (!nested.empty()) {
                if (!text.empty()) {
                    text.push_back(' ');
                }
                text += nested;
            }
        }
    }
    return text;
}

std::string ParseOdtContentXml(const std::string& xml) {
    pugi::xml_document document;
    const pugi::xml_parse_result parse_result = document.load_buffer(
        xml.data(),
        xml.size(),
        pugi::parse_default | pugi::parse_ws_pcdata_single,
        pugi::encoding_auto);
    if (!parse_result) {
        throw std::runtime_error(std::string("Unable to parse ODT XML: ") + parse_result.description());
    }

    pugi::xml_node text_node = FindFirstDescendantByLocalName(document, "text");
    if (!text_node) {
        throw std::runtime_error("Unable to parse ODT XML: office:text was not found.");
    }

    std::string output;
    AppendOdtBlocks(text_node, output);
    return TrimTrailingNewlines(output);
}
