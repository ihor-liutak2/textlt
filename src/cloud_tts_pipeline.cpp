#include "cloud_tts_pipeline.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

#include "editor_config.hpp"

namespace textlt {
namespace {

constexpr size_t kMinimumChunkBytes = 1229;
constexpr size_t kIdealChunkBytes = 1843;
constexpr size_t kMaximumChunkBytes = 2253;
constexpr size_t kMaximumWordLength = 25;

std::string JsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

bool IsSentenceBoundary(char character) {
    return character == '.' || character == '!' || character == '?';
}

std::string SplitIdentifierToken(const std::string& token) {
    std::string split;
    split.reserve(token.size() + 4);

    for (size_t index = 0; index < token.size(); ++index) {
        const unsigned char current = static_cast<unsigned char>(token[index]);
        if (current == '_') {
            if (!split.empty() && split.back() != ' ') {
                split.push_back(' ');
            }
            continue;
        }

        const unsigned char previous = index > 0
            ? static_cast<unsigned char>(token[index - 1])
            : 0;
        const unsigned char next = index + 1 < token.size()
            ? static_cast<unsigned char>(token[index + 1])
            : 0;

        const bool lower_to_upper =
            index > 0 && std::islower(previous) && std::isupper(current);
        const bool acronym_to_word =
            index > 0 &&
            index + 1 < token.size() &&
            std::isupper(previous) &&
            std::isupper(current) &&
            std::islower(next);

        if (!split.empty() && (lower_to_upper || acronym_to_word)) {
            split.push_back(' ');
        }
        split.push_back(static_cast<char>(current));
    }

    return split;
}

} // namespace

CloudTtsPipeline::CloudTtsPipeline() = default;

CloudTtsPipeline::~CloudTtsPipeline() {
    JoinWorker();
}

void CloudTtsPipeline::Submit(std::string entire_document_text, size_t current_cursor_line) {
    JoinWorker();
    worker_ = std::thread([text = std::move(entire_document_text), current_cursor_line] {
        // BuildDiagnostics still expects SourcePosition, so we construct one from the line number.
        SourcePosition start_pos = {current_cursor_line, 0};
        WriteDiagnostics(BuildDiagnostics(text, start_pos));
    });
}

std::vector<CloudTtsPipeline::ChunkDiagnostic> CloudTtsPipeline::BuildDiagnostics(
    const std::string& text,
    SourcePosition start_position) {
    std::vector<ChunkDiagnostic> diagnostics;
    SourcePosition position = start_position;
    const std::vector<std::string> chunks = BuildRawChunks(text);

    for (const std::string& chunk : chunks) {
        ChunkDiagnostic diagnostic;
        diagnostic.chunk_index = diagnostics.size();
        diagnostic.start_line = position.line;
        diagnostic.start_column = position.column;
        position = AdvancePosition(position, chunk);
        diagnostic.end_line = position.line;
        diagnostic.end_column = position.column;
        diagnostic.raw_size_bytes = chunk.size();
        diagnostic.cleansed_text = CleanseText(chunk);
        diagnostic.network_status = "simulated_prepared";
        diagnostics.push_back(std::move(diagnostic));
    }

    return diagnostics;
}

std::vector<std::string> CloudTtsPipeline::BuildRawChunks(const std::string& text) {
    std::vector<std::string> chunks;
    std::string current;

    for (const std::string& paragraph : SplitParagraphs(text)) {
        if (paragraph.empty()) {
            continue;
        }

        if (paragraph.size() > kMaximumChunkBytes) {
            if (!current.empty()) {
                chunks.push_back(std::move(current));
                current.clear();
            }
            std::vector<std::string> slices = SliceOversizedParagraph(paragraph);
            chunks.insert(
                chunks.end(),
                std::make_move_iterator(slices.begin()),
                std::make_move_iterator(slices.end()));
            continue;
        }

        if (current.empty() || current.size() + paragraph.size() <= kMaximumChunkBytes) {
            current += paragraph;
            continue;
        }

        chunks.push_back(std::move(current));
        current = paragraph;
    }

    if (!current.empty()) {
        chunks.push_back(std::move(current));
    }
    return chunks;
}

std::vector<std::string> CloudTtsPipeline::SplitParagraphs(const std::string& text) {
    std::vector<std::string> paragraphs;
    size_t start = 0;
    for (size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\n') {
            continue;
        }

        size_t end = index + 1;
        while (end < text.size() && text[end] == '\n') {
            ++end;
        }
        paragraphs.push_back(text.substr(start, end - start));
        start = end;
        index = end == 0 ? 0 : end - 1;
    }

    if (start < text.size()) {
        paragraphs.push_back(text.substr(start));
    }
    return paragraphs;
}

std::vector<std::string> CloudTtsPipeline::SliceOversizedParagraph(
    const std::string& paragraph) {
    std::vector<std::string> slices;
    size_t start = 0;
    while (start < paragraph.size()) {
        const size_t remaining = paragraph.size() - start;
        if (remaining <= kMaximumChunkBytes) {
            slices.push_back(paragraph.substr(start));
            break;
        }

        const size_t end = FindSliceEnd(paragraph, start);
        slices.push_back(paragraph.substr(start, end - start));
        start = end;
    }
    return slices;
}

size_t CloudTtsPipeline::FindSliceEnd(const std::string& text, size_t start) {
    const size_t minimum_end = std::min(text.size(), start + kMinimumChunkBytes);
    const size_t ideal_end = std::min(text.size(), start + kIdealChunkBytes);
    const size_t maximum_end = std::min(text.size(), start + kMaximumChunkBytes);

    for (size_t index = ideal_end; index > minimum_end; --index) {
        if (IsSentenceBoundary(text[index - 1])) {
            return index;
        }
    }

    size_t best_after_ideal = std::string::npos;
    for (size_t index = ideal_end; index < maximum_end; ++index) {
        if (IsSentenceBoundary(text[index])) {
            best_after_ideal = index + 1;
            break;
        }
    }
    if (best_after_ideal != std::string::npos) {
        return best_after_ideal;
    }

    for (size_t index = ideal_end; index > minimum_end; --index) {
        if (std::isspace(static_cast<unsigned char>(text[index - 1]))) {
            return index;
        }
    }
    return ideal_end > start ? ideal_end : maximum_end;
}

CloudTtsPipeline::SourcePosition CloudTtsPipeline::AdvancePosition(
    SourcePosition position,
    const std::string& text) {
    for (char character : text) {
        if (character == '\n') {
            ++position.line;
            position.column = 0;
        } else {
            ++position.column;
        }
    }
    return position;
}

std::string CloudTtsPipeline::CleanseText(const std::string& text) {
    std::string filtered;
    filtered.reserve(text.size());
    for (unsigned char character : text) {
        if (std::isalnum(character) ||
            character == ' ' ||
            character == '.' ||
            character == ',' ||
            character == '-' ||
            character == '_') {
            filtered.push_back(static_cast<char>(character));
        } else if (std::isspace(character)) {
            filtered.push_back(' ');
        } else {
            filtered.push_back(' ');
        }
    }

    std::istringstream filtered_stream(filtered);
    std::ostringstream split_tokens;
    std::string raw_token;
    bool first_split_token = true;
    while (filtered_stream >> raw_token) {
        if (!first_split_token) {
            split_tokens << ' ';
        }
        split_tokens << SplitIdentifierToken(raw_token);
        first_split_token = false;
    }

    std::istringstream stream(split_tokens.str());
    std::ostringstream cleansed;
    std::string token;
    bool first = true;
    while (stream >> token) {
        if (token.size() > kMaximumWordLength) {
            continue;
        }
        if (!first) {
            cleansed << ' ';
        }
        cleansed << token;
        first = false;
    }
    return cleansed.str();
}

std::filesystem::path CloudTtsPipeline::DiagnosticPath() {
    return EditorConfig::DefaultConfigPath().parent_path() / "debug_tts.json";
}

void CloudTtsPipeline::WriteDiagnostics(
    const std::vector<ChunkDiagnostic>& diagnostics) {
    const std::filesystem::path path = DiagnosticPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return;
    }

    file << "[\n";
    for (size_t index = 0; index < diagnostics.size(); ++index) {
        const ChunkDiagnostic& diagnostic = diagnostics[index];
        file << "  {\n";
        file << "    \"chunk_index\": " << diagnostic.chunk_index << ",\n";
        file << "    \"start_line\": " << diagnostic.start_line << ",\n";
        file << "    \"start_column\": " << diagnostic.start_column << ",\n";
        file << "    \"end_line\": " << diagnostic.end_line << ",\n";
        file << "    \"end_column\": " << diagnostic.end_column << ",\n";
        file << "    \"raw_size_bytes\": " << diagnostic.raw_size_bytes << ",\n";
        file << "    \"cleansed_text\": \"" << JsonEscape(diagnostic.cleansed_text) << "\",\n";
        file << "    \"network_status\": \"" << diagnostic.network_status << "\"\n";
        file << "  }";
        if (index + 1 < diagnostics.size()) {
            file << ",";
        }
        file << "\n";
    }
    file << "]\n";
}

void CloudTtsPipeline::JoinWorker() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

} // namespace textlt
