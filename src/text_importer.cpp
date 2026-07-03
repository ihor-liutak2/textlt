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


uint32_t Cp1252CodePoint(unsigned char c) {
    static constexpr std::array<uint32_t, 32> cp1252_80_9f = {{
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
    }};

    if (c < 0x80 || c >= 0xA0) {
        return c;
    }
    return cp1252_80_9f[c - 0x80];
}

void AppendCodepageByte(std::string& output, unsigned char value, int codepage) {
    if (codepage == 1251) {
        AppendUtf8CodePoint(output, Cp1251CodePoint(value));
        return;
    }
    AppendUtf8CodePoint(output, Cp1252CodePoint(value));
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


bool IsRtfDestinationControl(const std::string& word) {
    static const char* ignored_destinations[] = {
        "fonttbl", "colortbl", "stylesheet", "info", "pict", "object",
        "header", "footer", "headerl", "headerr", "headerf",
        "footerl", "footerr", "footerf", "annotation", "atnid",
        "generator", "datafield", "datastore", "themedata", "colorschememapping",
        "xmlnstbl", "listtable", "listoverridetable", "rsidtbl", "revtbl",
        "latentstyles", "pnseclvl", "fontemb", "fontfile", "filetbl",
        "fldinst", "shp", "nonshppict", "stylesheet", "userprops",
    };

    for (const char* value : ignored_destinations) {
        if (word == value) {
            return true;
        }
    }
    return false;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string NormalizeImportedPlainText(const std::string& text) {
    std::string output;
    bool previous_blank = false;

    for (const std::string& raw_line : SplitLines(text)) {
        const std::string line = NormalizeInlineWhitespace(raw_line);
        if (line.empty()) {
            if (!output.empty() && !previous_blank) {
                AppendBlankLine(output);
                previous_blank = true;
            }
            continue;
        }
        AppendLine(output, line);
        previous_blank = false;
    }

    return TrimTrailingNewlines(output);
}

std::string ParseRtfDocument(const std::string& rtf) {
    struct RtfState {
        bool skip_destination = false;
        int unicode_skip_count = 1;
        int codepage = 1252;
    };

    std::vector<RtfState> stack(1);
    std::string output;
    int pending_fallback_skip = 0;

    auto append_text_byte = [&](unsigned char value) {
        if (stack.back().skip_destination) {
            return;
        }
        AppendCodepageByte(output, value, stack.back().codepage);
    };

    auto append_code_point = [&](uint32_t value) {
        if (!stack.back().skip_destination) {
            AppendUtf8CodePoint(output, value);
        }
    };

    for (size_t i = 0; i < rtf.size(); ++i) {
        const unsigned char current = static_cast<unsigned char>(rtf[i]);

        if (current == '{') {
            stack.push_back(stack.back());
            pending_fallback_skip = 0;
            continue;
        }
        if (current == '}') {
            if (stack.size() > 1) {
                stack.pop_back();
            }
            pending_fallback_skip = 0;
            continue;
        }
        if (current == '\\') {
            if (i + 1 >= rtf.size()) {
                break;
            }
            const char next = rtf[++i];

            if (next == '\\' || next == '{' || next == '}') {
                if (pending_fallback_skip > 0) {
                    --pending_fallback_skip;
                } else {
                    append_text_byte(static_cast<unsigned char>(next));
                }
                continue;
            }
            if (next == '\'') {
                if (i + 2 < rtf.size()) {
                    const int high = HexValue(rtf[i + 1]);
                    const int low = HexValue(rtf[i + 2]);
                    if (high >= 0 && low >= 0) {
                        if (pending_fallback_skip > 0) {
                            --pending_fallback_skip;
                        } else {
                            append_text_byte(static_cast<unsigned char>((high << 4) | low));
                        }
                        i += 2;
                    }
                }
                continue;
            }
            if (next == '*') {
                stack.back().skip_destination = true;
                continue;
            }
            if (!std::isalpha(static_cast<unsigned char>(next))) {
                if (pending_fallback_skip > 0) {
                    --pending_fallback_skip;
                    continue;
                }
                if (stack.back().skip_destination) {
                    continue;
                }
                switch (next) {
                    case '~': output.push_back(' '); break;
                    case '-': output.push_back('-'); break;
                    case '_': output.push_back('-'); break;
                    default: break;
                }
                continue;
            }

            std::string word;
            word.push_back(next);
            while (i + 1 < rtf.size() && std::isalpha(static_cast<unsigned char>(rtf[i + 1]))) {
                word.push_back(rtf[++i]);
            }

            bool has_parameter = false;
            int parameter_sign = 1;
            int parameter = 0;
            if (i + 1 < rtf.size() && rtf[i + 1] == '-') {
                parameter_sign = -1;
                has_parameter = true;
                ++i;
            }
            while (i + 1 < rtf.size() && std::isdigit(static_cast<unsigned char>(rtf[i + 1]))) {
                has_parameter = true;
                parameter = parameter * 10 + (rtf[++i] - '0');
            }
            parameter *= parameter_sign;
            if (i + 1 < rtf.size() && rtf[i + 1] == ' ') {
                ++i;
            }

            if (IsRtfDestinationControl(word)) {
                stack.back().skip_destination = true;
            }
            if (word == "ansicpg" && has_parameter) {
                stack.back().codepage = parameter;
            } else if (word == "uc" && has_parameter) {
                stack.back().unicode_skip_count = std::max(0, parameter);
            }

            if (stack.back().skip_destination) {
                continue;
            }

            if (word == "par" || word == "line") {
                output.push_back('\n');
            } else if (word == "page") {
                AppendBlankLine(output);
            } else if (word == "tab") {
                output.push_back('\t');
            } else if (word == "emdash") {
                AppendUtf8CodePoint(output, 0x2014);
            } else if (word == "endash") {
                AppendUtf8CodePoint(output, 0x2013);
            } else if (word == "bullet") {
                AppendUtf8CodePoint(output, 0x2022);
            } else if (word == "lquote") {
                AppendUtf8CodePoint(output, 0x2018);
            } else if (word == "rquote") {
                AppendUtf8CodePoint(output, 0x2019);
            } else if (word == "ldblquote") {
                AppendUtf8CodePoint(output, 0x201C);
            } else if (word == "rdblquote") {
                AppendUtf8CodePoint(output, 0x201D);
            } else if (word == "u" && has_parameter) {
                int value = parameter;
                if (value < 0) {
                    value += 65536;
                }
                append_code_point(static_cast<uint32_t>(value));
                pending_fallback_skip = stack.back().unicode_skip_count;
            }
            continue;
        }

        if (pending_fallback_skip > 0) {
            --pending_fallback_skip;
            continue;
        }
        if (current == '\r' || current == '\n') {
            continue;
        }
        append_text_byte(current);
    }

    return NormalizeImportedPlainText(output);
}

int AttributeIntByLocalName(const pugi::xml_node& node, const std::string& local_name, int fallback) {
    for (pugi::xml_attribute attribute : node.attributes()) {
        std::string name = attribute.name();
        const size_t separator = name.find(':');
        if (separator != std::string::npos) {
            name = name.substr(separator + 1);
        }
        if (name == local_name) {
            return std::max(1, attribute.as_int(fallback));
        }
    }
    return fallback;
}

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
        if (format == TextImportFormat::Rtf) {
            return ImportRtfFile(path);
        }
        if (format == TextImportFormat::Odt) {
            return ImportOdtFile(path);
        }
        if (format == TextImportFormat::GoogleDocShortcut) {
            return ImportGoogleDocShortcut(path);
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
    if (extension == ".rtf") {
        return TextImportFormat::Rtf;
    }
    if (extension == ".odt" || extension == ".ott") {
        return TextImportFormat::Odt;
    }
    if (extension == ".gdoc") {
        return TextImportFormat::GoogleDocShortcut;
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


TextImportResult TextImporter::ImportRtfFile(const std::filesystem::path& path) const {
    try {
        return {true, ParseRtfDocument(ReadWholeFile(path)), ""};
    } catch (const std::exception& e) {
        return {false, "", e.what()};
    }
}

TextImportResult TextImporter::ImportOdtFile(const std::filesystem::path& path) const {
    try {
        const std::string content_xml = ReadArchiveEntry(path, "content.xml");
        return {true, ParseOdtContentXml(content_xml), ""};
    } catch (const std::exception& e) {
        return {false, "", e.what()};
    }
}

TextImportResult TextImporter::ImportGoogleDocShortcut(const std::filesystem::path& path) const {
    try {
        const std::string shortcut = ReadWholeFile(path);
        const bool looks_like_gdoc = shortcut.find("docs.google.com") != std::string::npos ||
            shortcut.find("application/vnd.google-apps.document") != std::string::npos;
        if (!looks_like_gdoc) {
            return {false, "", "The .gdoc file is not a Google Docs shortcut."};
        }
        return {
            false,
            "",
            "Local .gdoc files are shortcuts, not document content. Open the Google Drive connection in Remote Files and open the Google Doc there; TextLT will export it as plain text."};
    } catch (const std::exception& e) {
        return {false, "", e.what()};
    }
}

} // namespace textlt
