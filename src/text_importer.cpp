#include "text_importer.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace textlt {
namespace {

struct ArchiveReadDeleter {
    void operator()(struct archive* value) const {
        if (value) {
            archive_read_free(value);
        }
    }
};

using ArchiveReadPtr = std::unique_ptr<struct archive, ArchiveReadDeleter>;

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string TrimAsciiWhitespace(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::string NormalizeInlineWhitespace(const std::string& value) {
    std::string result;
    result.reserve(value.size());

    bool in_space = false;
    for (unsigned char c : value) {
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            if (!in_space) {
                result.push_back(' ');
                in_space = true;
            }
            continue;
        }

        result.push_back(static_cast<char>(c));
        in_space = false;
    }

    return TrimAsciiWhitespace(result);
}

void AppendUtf8CodePoint(std::string& output, uint32_t code_point) {
    if (code_point <= 0x7F) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | ((code_point >> 6) & 0x1F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | ((code_point >> 12) & 0x0F)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | ((code_point >> 18) & 0x07)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

uint32_t Cp1251CodePoint(unsigned char c) {
    static constexpr std::array<uint32_t, 64> cp1251_80_bf = {{
        0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
        0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
        0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
        0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
        0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
        0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
        0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    }};

    if (c < 0x80) {
        return c;
    }
    if (c >= 0x80 && c <= 0xBF) {
        return cp1251_80_bf[c - 0x80];
    }
    if (c >= 0xC0 && c <= 0xDF) {
        return 0x0410 + (c - 0xC0);
    }
    return 0x0430 + (c - 0xE0);
}

std::string Cp1251ToUtf8(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 2);

    for (unsigned char c : input) {
        AppendUtf8CodePoint(output, Cp1251CodePoint(c));
    }

    return output;
}

std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to open file: " + path.string());
    }

    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
}

std::string ArchiveError(struct archive* value) {
    const char* message = archive_error_string(value);
    return message ? message : "Unknown archive error";
}

std::string ReadArchiveEntry(
    const std::filesystem::path& archive_path,
    const std::string& entry_name) {
    ArchiveReadPtr archive_reader(archive_read_new());
    if (!archive_reader) {
        throw std::runtime_error("Unable to create archive reader.");
    }

    archive_read_support_filter_all(archive_reader.get());
    archive_read_support_format_all(archive_reader.get());

    const int open_result = archive_read_open_filename(
        archive_reader.get(),
        archive_path.string().c_str(),
        10240);
    if (open_result != ARCHIVE_OK) {
        throw std::runtime_error(
            "Unable to open archive: " + archive_path.string() + ". " +
            ArchiveError(archive_reader.get()));
    }

    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(archive_reader.get(), &entry) == ARCHIVE_OK) {
        const char* current_name = archive_entry_pathname(entry);
        if (!current_name || entry_name != current_name) {
            archive_read_data_skip(archive_reader.get());
            continue;
        }

        std::string content;
        const void* block = nullptr;
        size_t block_size = 0;
        la_int64_t block_offset = 0;
        while (true) {
            const int read_result = archive_read_data_block(
                archive_reader.get(),
                &block,
                &block_size,
                &block_offset);
            if (read_result == ARCHIVE_EOF) {
                break;
            }
            if (read_result != ARCHIVE_OK) {
                throw std::runtime_error(
                    "Unable to read archive entry: " + entry_name + ". " +
                    ArchiveError(archive_reader.get()));
            }
            content.append(static_cast<const char*>(block), block_size);
        }
        return content;
    }

    throw std::runtime_error("Archive entry not found: " + entry_name);
}

std::string ReadFirstFb2ArchiveEntry(const std::filesystem::path& archive_path) {
    ArchiveReadPtr archive_reader(archive_read_new());
    if (!archive_reader) {
        throw std::runtime_error("Unable to create archive reader.");
    }

    archive_read_support_filter_all(archive_reader.get());
    archive_read_support_format_all(archive_reader.get());

    if (archive_read_open_filename(
            archive_reader.get(), archive_path.string().c_str(), 10240) != ARCHIVE_OK) {
        throw std::runtime_error(
            "Unable to open archive: " + archive_path.string() + ". " +
            ArchiveError(archive_reader.get()));
    }

    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(archive_reader.get(), &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname(entry);
        const std::filesystem::path entry_path = pathname ? pathname : "";
        if (archive_entry_filetype(entry) != AE_IFREG ||
            ToLowerAscii(entry_path.extension().string()) != ".fb2") {
            archive_read_data_skip(archive_reader.get());
            continue;
        }

        std::string content;
        const void* block = nullptr;
        size_t block_size = 0;
        la_int64_t block_offset = 0;
        while (true) {
            const int read_result = archive_read_data_block(
                archive_reader.get(), &block, &block_size, &block_offset);
            if (read_result == ARCHIVE_EOF) {
                return content;
            }
            if (read_result != ARCHIVE_OK) {
                throw std::runtime_error(
                    "Unable to read FB2 archive entry: " + entry_path.string() + ". " +
                    ArchiveError(archive_reader.get()));
            }
            content.append(static_cast<const char*>(block), block_size);
        }
    }

    throw std::runtime_error(
        "Archive does not contain an .fb2 file: " + archive_path.string());
}

std::string ExtractXmlEncoding(const std::string& xml) {
    const size_t declaration_begin = xml.find("<?xml");
    if (declaration_begin == std::string::npos || declaration_begin > 8) {
        return "";
    }

    const size_t declaration_end = xml.find("?>", declaration_begin);
    if (declaration_end == std::string::npos) {
        return "";
    }

    std::string declaration = ToLowerAscii(
        xml.substr(declaration_begin, declaration_end - declaration_begin));
    const size_t encoding_pos = declaration.find("encoding");
    if (encoding_pos == std::string::npos) {
        return "";
    }

    const size_t equals_pos = declaration.find('=', encoding_pos);
    if (equals_pos == std::string::npos) {
        return "";
    }

    size_t quote_pos = equals_pos + 1;
    while (quote_pos < declaration.size() && std::isspace(static_cast<unsigned char>(declaration[quote_pos]))) {
        ++quote_pos;
    }
    if (quote_pos >= declaration.size() || (declaration[quote_pos] != '\'' && declaration[quote_pos] != '"')) {
        return "";
    }

    const char quote = declaration[quote_pos];
    const size_t value_begin = quote_pos + 1;
    const size_t value_end = declaration.find(quote, value_begin);
    if (value_end == std::string::npos) {
        return "";
    }

    return declaration.substr(value_begin, value_end - value_begin);
}

std::string ReplaceXmlEncodingWithUtf8(std::string xml) {
    const size_t declaration_begin = xml.find("<?xml");
    if (declaration_begin == std::string::npos || declaration_begin > 8) {
        return xml;
    }

    const size_t declaration_end = xml.find("?>", declaration_begin);
    if (declaration_end == std::string::npos) {
        return xml;
    }

    const size_t encoding_pos = ToLowerAscii(
        xml.substr(declaration_begin, declaration_end - declaration_begin)).find("encoding");
    if (encoding_pos == std::string::npos) {
        return xml;
    }

    const size_t absolute_encoding_pos = declaration_begin + encoding_pos;
    const size_t equals_pos = xml.find('=', absolute_encoding_pos);
    if (equals_pos == std::string::npos || equals_pos > declaration_end) {
        return xml;
    }

    size_t quote_pos = equals_pos + 1;
    while (quote_pos < declaration_end && std::isspace(static_cast<unsigned char>(xml[quote_pos]))) {
        ++quote_pos;
    }
    if (quote_pos >= declaration_end || (xml[quote_pos] != '\'' && xml[quote_pos] != '"')) {
        return xml;
    }

    const char quote = xml[quote_pos];
    const size_t value_begin = quote_pos + 1;
    const size_t value_end = xml.find(quote, value_begin);
    if (value_end == std::string::npos || value_end > declaration_end) {
        return xml;
    }

    xml.replace(value_begin, value_end - value_begin, "UTF-8");
    return xml;
}

std::string NormalizeXmlInputEncoding(const std::string& xml) {
    const std::string encoding = ExtractXmlEncoding(xml);
    if (encoding == "windows-1251" || encoding == "cp1251" || encoding == "x-cp1251") {
        return ReplaceXmlEncodingWithUtf8(Cp1251ToUtf8(xml));
    }
    return xml;
}

std::string LocalName(const pugi::xml_node& node) {
    const std::string name = node.name();
    const size_t separator = name.find(':');
    if (separator == std::string::npos) {
        return name;
    }
    return name.substr(separator + 1);
}

bool IsNode(const pugi::xml_node& node, const std::string& local_name) {
    return LocalName(node) == local_name;
}

bool IsAnyNode(const pugi::xml_node& node, std::initializer_list<const char*> names) {
    const std::string local_name = LocalName(node);
    return std::any_of(names.begin(), names.end(), [&](const char* name) {
        return local_name == name;
    });
}

pugi::xml_node FindFirstDescendantByLocalName(
    const pugi::xml_node& node,
    const std::string& local_name) {
    for (pugi::xml_node child : node.children()) {
        if (IsNode(child, local_name)) {
            return child;
        }

        pugi::xml_node nested = FindFirstDescendantByLocalName(child, local_name);
        if (nested) {
            return nested;
        }
    }

    return {};
}

void AppendLine(std::string& output, const std::string& line) {
    if (!output.empty() && output.back() != '\n') {
        output.push_back('\n');
    }
    output += line;
    output.push_back('\n');
}

void AppendBlankLine(std::string& output) {
    if (output.empty()) {
        output.push_back('\n');
        return;
    }
    if (output.back() != '\n') {
        output.push_back('\n');
    }
    if (output.size() < 2 || output[output.size() - 2] != '\n') {
        output.push_back('\n');
    }
}

std::string TrimTrailingNewlines(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

void AppendNonEmptyNormalizedLine(std::string& output, const std::string& value) {
    const std::string line = NormalizeInlineWhitespace(value);
    if (!line.empty()) {
        AppendLine(output, line);
    }
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream stream(text);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    if (lines.empty()) {
        lines.push_back(text);
    }
    return lines;
}

std::string JoinCells(const std::vector<std::string>& cells) {
    std::string row;
    for (const std::string& cell : cells) {
        if (!row.empty()) {
            row += "; ";
        }
        row += NormalizeInlineWhitespace(cell);
    }
    return TrimAsciiWhitespace(row);
}

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

bool TextImportEntryLess(const TextImportEntry& left, const TextImportEntry& right) {
    if (left.kind == TextImportEntryKind::ParentDirectory) {
        return true;
    }
    if (right.kind == TextImportEntryKind::ParentDirectory) {
        return false;
    }
    if (left.kind != right.kind) {
        return left.kind == TextImportEntryKind::Directory;
    }
    return ToLowerAscii(left.name) < ToLowerAscii(right.name);
}

} // namespace

TextImportResult TextImporter::ImportFile(const std::filesystem::path& path) const {
    try {
        std::error_code error_code;
        if (!std::filesystem::exists(path, error_code)) {
            return {false, "", "File does not exist: " + path.string()};
        }
        if (!std::filesystem::is_regular_file(path, error_code)) {
            return {false, "", "Path is not a regular file: " + path.string()};
        }

        const TextImportFormat format = DetectFormat(path);
        if (format == TextImportFormat::Fb2) {
            return ImportFb2File(path);
        }
        if (format == TextImportFormat::Fb2Zip) {
            return ImportFb2ZipFile(path);
        }
        if (format == TextImportFormat::Docx) {
            return ImportDocxFile(path);
        }

        return {false, "", "Unsupported import file type: " + path.string()};
    } catch (const std::exception& e) {
        return {false, "", e.what()};
    }
}

std::vector<TextImportEntry> TextImporter::ListDirectory(
    const std::filesystem::path& directory,
    std::string& error) const {
    error.clear();
    std::vector<TextImportEntry> entries;

    std::error_code error_code;
    std::filesystem::path current = directory.empty()
        ? std::filesystem::current_path(error_code)
        : std::filesystem::absolute(directory, error_code);
    if (error_code) {
        error = "Unable to resolve directory: " + directory.string();
        return entries;
    }

    current = std::filesystem::weakly_canonical(current, error_code);
    if (error_code) {
        error.clear();
        current = directory;
    }

    if (!std::filesystem::exists(current, error_code) || !std::filesystem::is_directory(current, error_code)) {
        error = "Path is not a directory: " + current.string();
        return entries;
    }

    const std::filesystem::path parent = current.parent_path();
    if (!parent.empty() && parent != current) {
        entries.push_back({parent, "..", TextImportEntryKind::ParentDirectory, TextImportFormat::Unsupported});
    }

    std::filesystem::directory_iterator iterator(current, error_code);
    if (error_code) {
        error = "Unable to read directory: " + current.string();
        return entries;
    }

    for (const std::filesystem::directory_entry& entry : iterator) {
        const std::filesystem::path entry_path = entry.path();
        if (entry.is_directory(error_code)) {
            entries.push_back({
                entry_path,
                entry_path.filename().string(),
                TextImportEntryKind::Directory,
                TextImportFormat::Unsupported});
            continue;
        }

        if (!entry.is_regular_file(error_code)) {
            continue;
        }

        const TextImportFormat format = DetectFormat(entry_path);
        if (format == TextImportFormat::Unsupported) {
            continue;
        }

        entries.push_back({
            entry_path,
            entry_path.filename().string(),
            TextImportEntryKind::File,
            format});
    }

    std::sort(entries.begin(), entries.end(), TextImportEntryLess);
    return entries;
}

TextImportFormat TextImporter::DetectFormat(const std::filesystem::path& path) {
    const std::string filename = ToLowerAscii(path.filename().string());
    if (filename.size() >= 8 &&
        filename.compare(filename.size() - 8, 8, ".fb2.zip") == 0) {
        return TextImportFormat::Fb2Zip;
    }

    const std::string extension = ToLowerAscii(path.extension().string());
    if (extension == ".fb2") {
        return TextImportFormat::Fb2;
    }
    if (extension == ".docx") {
        return TextImportFormat::Docx;
    }
    return TextImportFormat::Unsupported;
}

bool TextImporter::IsSupportedFile(const std::filesystem::path& path) {
    return DetectFormat(path) != TextImportFormat::Unsupported;
}

TextImportResult TextImporter::ImportFb2File(const std::filesystem::path& path) const {
    try {
        const std::string content = ReadWholeFile(path);
        return {true, ParseFb2Xml(content), ""};
    } catch (const std::exception& e) {
        return {false, "", e.what()};
    }
}

TextImportResult TextImporter::ImportFb2ZipFile(const std::filesystem::path& path) const {
    try {
        return {true, ParseFb2Xml(ReadFirstFb2ArchiveEntry(path)), ""};
    } catch (const std::exception& e) {
        return {false, "", e.what()};
    }
}

TextImportResult TextImporter::ImportDocxFile(const std::filesystem::path& path) const {
    try {
        const std::string document_xml = ReadArchiveEntry(path, "word/document.xml");
        return {true, ParseDocxDocumentXml(document_xml), ""};
    } catch (const std::exception& e) {
        return {false, "", e.what()};
    }
}

} // namespace textlt
