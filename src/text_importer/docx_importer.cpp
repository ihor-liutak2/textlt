/* Included by ../text_importer.cpp. */

std::string ExtractDocxInlineText(const pugi::xml_node& node) {
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
        if (local_name == "t" || local_name == "delText" || local_name == "instrText") {
            text += child.text().get();
        } else if (local_name == "tab") {
            text.push_back('\t');
        } else if (local_name == "br" || local_name == "cr") {
            text.push_back('\n');
        } else if (local_name == "noBreakHyphen") {
            text.push_back('-');
        } else if (local_name == "softHyphen") {
            // Soft hyphens are layout hints and should not appear in imported plain text.
        } else {
            text += ExtractDocxInlineText(child);
        }
    }

    return text;
}

std::string ExtractDocxParagraph(const pugi::xml_node& paragraph) {
    return ExtractDocxInlineText(paragraph);
}

std::string ExtractDocxCellText(const pugi::xml_node& cell);

void AppendDocxTable(const pugi::xml_node& table, std::string& output) {
    for (pugi::xml_node row : table.children()) {
        if (!IsNode(row, "tr")) {
            continue;
        }

        std::vector<std::string> cells;
        for (pugi::xml_node cell : row.children()) {
            if (IsNode(cell, "tc")) {
                cells.push_back(ExtractDocxCellText(cell));
            }
        }

        const std::string row_text = JoinCells(cells);
        if (!row_text.empty()) {
            AppendLine(output, row_text);
        }
    }
}

void AppendDocxBlocks(const pugi::xml_node& container, std::string& output) {
    for (pugi::xml_node child : container.children()) {
        if (IsNode(child, "p")) {
            const std::string paragraph = NormalizeInlineWhitespace(ExtractDocxParagraph(child));
            if (!paragraph.empty()) {
                AppendLine(output, paragraph);
            } else {
                AppendBlankLine(output);
            }
        } else if (IsNode(child, "tbl")) {
            AppendDocxTable(child, output);
            AppendBlankLine(output);
        } else if (IsNode(child, "AlternateContent")) {
            // Word uses mc:AlternateContent around some tables and other blocks.
            // Read one representation only, otherwise Choice and Fallback duplicate text.
            pugi::xml_node representation;
            for (pugi::xml_node candidate : child.children()) {
                if (IsNode(candidate, "Choice")) {
                    representation = candidate;
                    break;
                }
                if (!representation && IsNode(candidate, "Fallback")) {
                    representation = candidate;
                }
            }
            if (representation) {
                AppendDocxBlocks(representation, output);
            }
        } else if (child.type() == pugi::node_element) {
            // Tables and paragraphs may be wrapped in customXml, smart tags,
            // revision marks, or other Word-specific containers.
            AppendDocxBlocks(child, output);
        }
    }
}

std::string ExtractDocxCellText(const pugi::xml_node& cell) {
    std::string text;
    for (pugi::xml_node child : cell.children()) {
        if (IsNode(child, "p")) {
            const std::string paragraph = NormalizeInlineWhitespace(ExtractDocxParagraph(child));
            if (!paragraph.empty()) {
                if (!text.empty()) {
                    text.push_back(' ');
                }
                text += paragraph;
            }
        } else if (IsNode(child, "tbl")) {
            std::string nested_table;
            AppendDocxTable(child, nested_table);
            nested_table = NormalizeInlineWhitespace(nested_table);
            if (!nested_table.empty()) {
                if (!text.empty()) {
                    text.push_back(' ');
                }
                text += nested_table;
            }
        } else if (child.type() == pugi::node_element) {
            const std::string nested = NormalizeInlineWhitespace(ExtractDocxInlineText(child));
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

std::string ParseDocxDocumentXml(const std::string& xml) {
    pugi::xml_document document;
    const pugi::xml_parse_result parse_result = document.load_buffer(
        xml.data(),
        xml.size(),
        pugi::parse_default | pugi::parse_ws_pcdata_single,
        pugi::encoding_auto);
    if (!parse_result) {
        throw std::runtime_error(std::string("Unable to parse DOCX XML: ") + parse_result.description());
    }

    pugi::xml_node body = FindFirstDescendantByLocalName(document, "body");
    if (!body) {
        throw std::runtime_error("Unable to parse DOCX XML: document body was not found.");
    }

    std::string output;
    AppendDocxBlocks(body, output);
    return TrimTrailingNewlines(output);
}
