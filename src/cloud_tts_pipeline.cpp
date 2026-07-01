#include "cloud_tts_pipeline.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <system_error>

#include "json_utils.hpp"

namespace textlt {
namespace {

constexpr size_t kMinimumChunkBytes = 1229;
constexpr size_t kIdealChunkBytes = 1843;
constexpr size_t kMaximumChunkBytes = 2253;
constexpr size_t kMaximumWordLength = 25;

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

unsigned long long StableHash(const std::string& text) {
    unsigned long long hash = 14695981039346656037ull;
    for (unsigned char character : text) {
        hash ^= character;
        hash *= 1099511628211ull;
    }
    return hash;
}


std::string SafePathSegment(const std::string& value) {
    std::string segment;
    segment.reserve(value.size());
    for (unsigned char character : value) {
        if (std::isalnum(character) || character == '-' || character == '_' || character == '.') {
            segment.push_back(static_cast<char>(character));
        } else {
            segment.push_back('_');
        }
    }
    return segment.empty() ? "default" : segment;
}

std::string QuoteShellPath(const std::filesystem::path& path) {
#ifdef _WIN32
    std::string value = path.string();
    std::string quoted = "\"";
    for (char character : value) {
        if (character == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(character);
        }
    }
    quoted += "\"";
    return quoted;
#else
    const std::string value = path.string();
    std::string quoted = "'";
    for (char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted += "'";
    return quoted;
#endif
}


Json LoadJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Json();
    }
    return Json::parse(file, nullptr, false);
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << text;
    return static_cast<bool>(file);
}

bool RunPiperCommand(const std::filesystem::path& executable,
                     const std::filesystem::path& model,
                     const std::filesystem::path& config,
                     const std::filesystem::path& input_text,
                     const std::filesystem::path& output_wav) {
#ifdef _WIN32
    const std::string command =
        "type " + QuoteShellPath(input_text) +
        " | " + QuoteShellPath(executable) +
        " --model " + QuoteShellPath(model) +
        " --config " + QuoteShellPath(config) +
        " --output_file " + QuoteShellPath(output_wav);
#else
    const std::string command =
        QuoteShellPath(executable) +
        " --model " + QuoteShellPath(model) +
        " --config " + QuoteShellPath(config) +
        " --output_file " + QuoteShellPath(output_wav) +
        " < " + QuoteShellPath(input_text);
#endif
    return std::system(command.c_str()) == 0;
}

} // namespace

CloudTtsPipeline::CloudTtsPipeline() = default;

CloudTtsPipeline::~CloudTtsPipeline() {
    JoinWorker();
}

std::vector<CloudTtsPipeline::BookInfo> CloudTtsPipeline::ListLocalBooks() const {
    std::vector<BookInfo> books;
    const std::filesystem::path library = LibraryDirectory();
    if (library.empty()) {
        return books;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(library, error)) {
        return books;
    }

    for (const auto& entry : std::filesystem::directory_iterator(library, error)) {
        if (error) {
            break;
        }
        if (!entry.is_directory(error) || error) {
            error.clear();
            continue;
        }

        BookInfo book;
        if (LoadBookInfo(entry.path(), &book)) {
            books.push_back(std::move(book));
        }
    }

    std::sort(books.begin(), books.end(), [](const BookInfo& left, const BookInfo& right) {
        const auto left_key = std::make_tuple(
            left.series,
            left.series_index,
            left.title.empty() ? left.source_file_name : left.title,
            left.source_file_name);
        const auto right_key = std::make_tuple(
            right.series,
            right.series_index,
            right.title.empty() ? right.source_file_name : right.title,
            right.source_file_name);
        return left_key < right_key;
    });
    return books;
}

bool CloudTtsPipeline::LoadBookMetadata(const std::string& book_id, BookMetadata* book) const {
    if (!book || book_id.empty()) {
        return false;
    }

    const std::filesystem::path book_directory = BookDirectory(book_id);
    if (book_directory.empty()) {
        return false;
    }
    const Json book_json = LoadJsonObject(book_directory / "book.json");
    if (book_json.empty()) {
        return false;
    }

    BookMetadata loaded;
    loaded.book_id = JsonString(book_json, "book_id", book_id);
    loaded.source_file_path = JsonString(book_json, "source_file_path");
    loaded.file_name = JsonString(book_json, "file_name");
    loaded.file_size = static_cast<uintmax_t>(JsonSize(book_json, "file_size", 0));
    const auto modified = book_json.find("modified_time");
    if (modified != book_json.end() && modified->is_number_integer()) {
        loaded.modified_time = modified->get<long long>();
    }
    loaded.title = JsonString(book_json, "title");
    loaded.author = JsonString(book_json, "author");
    loaded.series = JsonString(book_json, "series");
    loaded.genre = JsonString(book_json, "genre");
    loaded.series_index = JsonInt(book_json, "series_index", 0);
    loaded.language = JsonString(book_json, "language");
    loaded.piper_voice_id = JsonString(book_json, "piper_voice_id");
    loaded.last_cursor_line = JsonSize(book_json, "last_cursor_line", 0);

    if (loaded.book_id.empty()) {
        return false;
    }
    *book = std::move(loaded);
    return true;
}

bool CloudTtsPipeline::UpdateBookMetadata(
    const std::string& book_id,
    const EditableBookMetadata& metadata) const {
    if (book_id.empty()) {
        return false;
    }

    const std::filesystem::path book_directory = BookDirectory(book_id);
    if (book_directory.empty()) {
        return false;
    }
    const std::filesystem::path book_path = book_directory / "book.json";
    Json book_json = LoadJsonObject(book_path);
    if (book_json.empty()) {
        return false;
    }

    book_json["title"] = metadata.title;
    book_json["author"] = metadata.author;
    book_json["series"] = metadata.series;
    book_json["genre"] = metadata.genre;
    book_json["series_index"] = metadata.series_index;
    book_json["language"] = metadata.language;
    book_json["piper_voice_id"] = metadata.piper_voice_id;
    return WriteJsonAtomically(book_path, book_json);
}

CloudTtsPipeline::MetadataSuggestions CloudTtsPipeline::BuildMetadataSuggestions(
    const std::vector<BookInfo>& books) const {
    std::set<std::string> series_values;
    std::set<std::string> genre_values;
    for (const BookInfo& book : books) {
        if (!book.series.empty()) {
            series_values.insert(book.series);
        }
        if (!book.genre.empty()) {
            genre_values.insert(book.genre);
        }
    }

    MetadataSuggestions suggestions;
    suggestions.series.assign(series_values.begin(), series_values.end());
    suggestions.genres.assign(genre_values.begin(), genre_values.end());
    return suggestions;
}

std::vector<CloudTtsPipeline::LanguageOption> CloudTtsPipeline::ListLanguageOptions() const {
    std::vector<LanguageOption> options = {{"unknown", "Unknown"}};
    const std::filesystem::path registry_path =
        RegistryDirectory() / "piper_voices_index.json";
    std::ifstream file(registry_path, std::ios::binary);
    if (!file) {
        return options;
    }

    const Json root = Json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return options;
    }

    std::set<std::string> seen_codes = {"unknown"};
    const auto voices = root.find("voices");
    if (voices == root.end() || !voices->is_array()) {
        return options;
    }

    for (const Json& voice : *voices) {
        if (!voice.is_object()) {
            continue;
        }
        const std::string code = JsonString(voice, "language_code");
        if (code.empty() || !seen_codes.insert(code).second) {
            continue;
        }
        const std::string name = JsonString(voice, "language_name", code);
        const std::string country = JsonString(voice, "country");
        const std::string label =
            (country.empty() ? name : name + " - " + country) + " (" + code + ")";
        options.push_back({code, label});
    }
    return options;
}

std::vector<CloudTtsPipeline::PiperVoiceOption> CloudTtsPipeline::ListPiperVoiceOptions(
    const std::string& language_code) const {
    std::vector<PiperVoiceOption> options;
    if (language_code.empty() || language_code == "unknown") {
        return options;
    }

    const std::filesystem::path registry_path =
        RegistryDirectory() / "piper_voices_index.json";
    std::ifstream file(registry_path, std::ios::binary);
    if (!file) {
        return options;
    }

    const Json root = Json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return options;
    }

    const auto voices = root.find("voices");
    if (voices == root.end() || !voices->is_array()) {
        return options;
    }

    for (const Json& voice : *voices) {
        if (!voice.is_object() || JsonString(voice, "language_code") != language_code) {
            continue;
        }
        const std::string id = JsonString(voice, "id");
        if (id.empty()) {
            continue;
        }
        const bool installed = PiperVoiceInstalled(voice);
        std::string label = id;
        const std::string quality = JsonString(voice, "quality");
        const std::string speaker = JsonString(voice, "speaker");
        if (!speaker.empty()) {
            label += " | " + speaker;
        }
        if (!quality.empty()) {
            label += " | " + quality;
        }
        label += installed ? " | installed" : " | not installed";
        options.push_back({id, label, installed});
    }
    return options;
}


bool CloudTtsPipeline::PiperRuntimeInstalled() const {
    return !PiperExecutablePath().empty();
}

bool CloudTtsPipeline::GenerateChunkAudio(
    const std::string& book_id,
    size_t chunk_index,
    const std::string& voice_id,
    std::string* error) const {
    if (book_id.empty()) {
        if (error) {
            *error = "Book id is empty";
        }
        return false;
    }
    if (voice_id.empty()) {
        if (error) {
            *error = "Voice is not selected";
        }
        return false;
    }

    const std::filesystem::path executable = PiperExecutablePath();
    if (executable.empty()) {
        if (error) {
            *error = "Piper runtime is not installed";
        }
        return false;
    }

    Json voice;
    if (!FindPiperVoiceById(voice_id, &voice)) {
        if (error) {
            *error = "Piper voice is not in registry";
        }
        return false;
    }
    if (!PiperVoiceInstalled(voice)) {
        if (error) {
            *error = "Piper voice files are missing";
        }
        return false;
    }

    const std::filesystem::path book_directory = BookDirectory(book_id);
    const std::filesystem::path chunks_path = book_directory / "chunks.json";
    Json chunks_json = LoadJsonFile(chunks_path);
    if (!chunks_json.is_array() || chunk_index >= chunks_json.size()) {
        if (error) {
            *error = "Chunk index is out of range";
        }
        return false;
    }

    Json& chunk = chunks_json[chunk_index];
    const std::string text = JsonString(chunk, "cleansed_text");
    if (text.empty()) {
        chunk["status"] = "failed";
        chunk["audio_path"] = "";
        WriteJsonAtomically(chunks_path, chunks_json);
        if (error) {
            *error = "Chunk text is empty";
        }
        return false;
    }

    const std::filesystem::path output_wav = ChunkAudioPath(book_id, chunk_index, voice_id);
    const std::filesystem::path relative_wav = RelativeChunkAudioPath(chunk_index, voice_id);
    std::error_code exists_error;
    if (std::filesystem::exists(output_wav, exists_error)) {
        chunk["status"] = "ready";
        chunk["audio_path"] = relative_wav.generic_string();
        return WriteJsonAtomically(chunks_path, chunks_json);
    }

    std::error_code create_error;
    std::filesystem::create_directories(output_wav.parent_path(), create_error);
    if (create_error) {
        if (error) {
            *error = "Cannot create audio cache directory";
        }
        return false;
    }

    const std::filesystem::path input_path = output_wav.string() + ".txt";
    if (!WriteTextFile(input_path, text + "\n")) {
        if (error) {
            *error = "Cannot write Piper input file";
        }
        return false;
    }

    const std::filesystem::path model = PiperModelsDirectory() / JsonString(voice, "model_path");
    const std::filesystem::path config = PiperModelsDirectory() / JsonString(voice, "config_path");
    const bool ok = RunPiperCommand(executable, model, config, input_path, output_wav);
    std::error_code cleanup_error;
    std::filesystem::remove(input_path, cleanup_error);

    exists_error.clear();
    const bool output_exists = std::filesystem::exists(output_wav, exists_error);
    if (!ok || !output_exists) {
        chunk["status"] = "failed";
        chunk["audio_path"] = "";
        WriteJsonAtomically(chunks_path, chunks_json);
        if (error) {
            *error = "Piper generation failed";
        }
        return false;
    }

    chunk["status"] = "ready";
    chunk["audio_path"] = relative_wav.generic_string();
    return WriteJsonAtomically(chunks_path, chunks_json);
}

CloudTtsPipeline::AudioGenerationResult CloudTtsPipeline::EnsureAudioLookahead(
    const std::string& book_id,
    size_t start_chunk_index,
    const std::string& voice_id,
    size_t lookahead_count,
    std::string* error) const {
    AudioGenerationResult result;
    if (lookahead_count == 0) {
        return result;
    }

    const std::filesystem::path chunks_path = BookDirectory(book_id) / "chunks.json";
    const Json chunks_json = LoadJsonFile(chunks_path);
    if (!chunks_json.is_array()) {
        if (error) {
            *error = "Cannot load chunks";
        }
        return result;
    }

    const size_t end = std::min(chunks_json.size(), start_chunk_index + lookahead_count);
    std::string last_error;
    for (size_t index = start_chunk_index; index < end; ++index) {
        ++result.requested_chunks;
        const std::filesystem::path output_wav = ChunkAudioPath(book_id, index, voice_id);
        std::error_code exists_error;
        if (std::filesystem::exists(output_wav, exists_error)) {
            ++result.already_ready_chunks;
            continue;
        }
        std::string chunk_error;
        if (GenerateChunkAudio(book_id, index, voice_id, &chunk_error)) {
            ++result.generated_chunks;
        } else {
            ++result.failed_chunks;
            if (!chunk_error.empty()) {
                last_error = chunk_error;
            }
        }
    }

    if (result.failed_chunks > 0 && error) {
        *error = last_error.empty() ? "Some chunks failed" : last_error;
    }
    return result;
}

bool CloudTtsPipeline::ClearBookAudioCache(const std::string& book_id, std::string* error) const {
    if (book_id.empty()) {
        if (error) {
            *error = "Book id is empty";
        }
        return false;
    }

    const std::filesystem::path book_directory = BookDirectory(book_id);
    const std::filesystem::path audio_directory = book_directory / "audio";
    std::error_code remove_error;
    std::filesystem::remove_all(audio_directory, remove_error);
    if (remove_error) {
        if (error) {
            *error = "Cannot remove audio cache";
        }
        return false;
    }
    std::error_code create_error;
    std::filesystem::create_directories(audio_directory, create_error);

    const std::filesystem::path chunks_path = book_directory / "chunks.json";
    Json chunks_json = LoadJsonFile(chunks_path);
    if (chunks_json.is_array()) {
        for (Json& chunk : chunks_json) {
            if (!chunk.is_object()) {
                continue;
            }
            chunk["status"] = "prepared";
            chunk["audio_path"] = "";
        }
        WriteJsonAtomically(chunks_path, chunks_json);
    }
    return true;
}

uintmax_t CloudTtsPipeline::BookAudioCacheSize(const std::string& book_id) const {
    const std::filesystem::path audio_directory = BookDirectory(book_id) / "audio";
    uintmax_t total = 0;
    std::error_code error;
    if (!std::filesystem::exists(audio_directory, error)) {
        return total;
    }
    std::filesystem::recursive_directory_iterator iterator(audio_directory, error);
    std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        if (entry.is_regular_file(error)) {
            error.clear();
            total += entry.file_size(error);
        }
        error.clear();
        iterator.increment(error);
    }
    return total;
}

size_t CloudTtsPipeline::FindChunkIndexForLine(const std::string& book_id, size_t line) const {
    const Json chunks_json = LoadJsonFile(BookDirectory(book_id) / "chunks.json");
    if (!chunks_json.is_array() || chunks_json.empty()) {
        return 0;
    }
    for (size_t index = 0; index < chunks_json.size(); ++index) {
        const Json& chunk = chunks_json[index];
        if (!chunk.is_object()) {
            continue;
        }
        const size_t start_line = JsonSize(chunk, "start_line", 0);
        const size_t end_line = JsonSize(chunk, "end_line", start_line);
        if (line >= start_line && line <= end_line) {
            return index;
        }
    }
    return 0;
}

void CloudTtsPipeline::Submit(
    std::string entire_document_text,
    std::filesystem::path source_file_path,
    size_t current_cursor_line) {
    JoinWorker();
    worker_ = std::thread([
        text = std::move(entire_document_text),
        source_file_path = std::move(source_file_path),
        current_cursor_line] {
        // Chunk positions must always map to the full document, not to the
        // cursor position used to choose the initial TTS chunk.
        SourcePosition start_pos = {0, 0};
        const BookMetadata book =
            BuildBookMetadata(source_file_path, text, current_cursor_line);
        WriteBook(book, BuildPreparedChunks(text, start_pos));
    });
}

std::vector<CloudTtsPipeline::PreparedChunk> CloudTtsPipeline::BuildPreparedChunks(
    const std::string& text,
    SourcePosition start_position) {
    std::vector<PreparedChunk> chunks;
    SourcePosition position = start_position;
    const std::vector<std::string> raw_chunks = BuildRawChunks(text);

    for (const std::string& raw_chunk : raw_chunks) {
        PreparedChunk chunk;
        chunk.chunk_index = chunks.size();
        chunk.start_line = position.line;
        chunk.start_column = position.column;
        position = AdvancePosition(position, raw_chunk);
        chunk.end_line = position.line;
        chunk.end_column = position.column;
        chunk.raw_size_bytes = raw_chunk.size();
        chunk.cleansed_text = CleanseText(raw_chunk);
        chunk.status = "prepared";
        chunks.push_back(std::move(chunk));
    }

    return chunks;
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

    bool previous_space = false;
    for (unsigned char character : text) {
        const bool whitespace = character == ' ' ||
                                character == '\t' ||
                                character == '\n' ||
                                character == '\r' ||
                                character == '\f' ||
                                character == '\v';
        if (whitespace) {
            if (!filtered.empty() && !previous_space) {
                filtered.push_back(' ');
            }
            previous_space = true;
            continue;
        }

        if (character < 32 || character == 127) {
            continue;
        }

        filtered.push_back(static_cast<char>(character));
        previous_space = false;
    }

    while (!filtered.empty() && filtered.back() == ' ') {
        filtered.pop_back();
    }
    return filtered;
}
std::filesystem::path CloudTtsPipeline::UserDataDirectory() {
#ifdef _WIN32
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data && !std::string(local_app_data).empty()) {
        return std::filesystem::path(local_app_data) / "textlt";
    }
    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && !std::string(user_profile).empty()) {
        return std::filesystem::path(user_profile) / "AppData" / "Local" / "textlt";
    }
    return {};
#else
    const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home && !std::string(xdg_data_home).empty()) {
        return std::filesystem::path(xdg_data_home) / "textlt";
    }
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return {};
    }
    return std::filesystem::path(home) / ".local" / "share" / "textlt";
#endif
}

std::filesystem::path CloudTtsPipeline::LibraryDirectory() {
    const std::filesystem::path data = UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "tts" / "library" / "books";
}

std::filesystem::path CloudTtsPipeline::BookDirectory(const std::string& book_id) {
    const std::filesystem::path library = LibraryDirectory();
    return library.empty() ? std::filesystem::path{} : library / book_id;
}

std::filesystem::path CloudTtsPipeline::RegistryDirectory() {
    const std::filesystem::path data = UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "registries";
}

std::filesystem::path CloudTtsPipeline::PiperRuntimeDirectory() {
    const std::filesystem::path data = UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "piper" / "bin";
}

std::filesystem::path CloudTtsPipeline::PiperModelsDirectory() {
    const std::filesystem::path data = UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "piper" / "models";
}

std::filesystem::path CloudTtsPipeline::PiperExecutablePath() {
    const std::filesystem::path runtime_directory = PiperRuntimeDirectory();
    if (runtime_directory.empty()) {
        return {};
    }
    std::error_code error;
    if (!std::filesystem::exists(runtime_directory, error)) {
        return {};
    }
#ifdef _WIN32
    constexpr const char* binary_name = "piper.exe";
#else
    constexpr const char* binary_name = "piper";
#endif
    std::filesystem::recursive_directory_iterator iterator(runtime_directory, error);
    std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        if (entry.is_regular_file(error) && entry.path().filename() == binary_name) {
            return entry.path();
        }
        error.clear();
        iterator.increment(error);
    }
    return {};
}

bool CloudTtsPipeline::PiperVoiceInstalled(const Json& voice) {
    const std::string model_path = JsonString(voice, "model_path");
    const std::string config_path = JsonString(voice, "config_path");
    if (model_path.empty() || config_path.empty()) {
        return false;
    }

    const std::filesystem::path models_directory = PiperModelsDirectory();
    std::error_code error;
    return std::filesystem::exists(models_directory / model_path, error) &&
           std::filesystem::exists(models_directory / config_path, error);
}

bool CloudTtsPipeline::FindPiperVoiceById(const std::string& voice_id, Json* voice) {
    if (!voice || voice_id.empty()) {
        return false;
    }

    const std::filesystem::path registry_path =
        RegistryDirectory() / "piper_voices_index.json";
    std::ifstream file(registry_path, std::ios::binary);
    if (!file) {
        return false;
    }

    const Json root = Json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return false;
    }

    const auto voices = root.find("voices");
    if (voices == root.end() || !voices->is_array()) {
        return false;
    }

    for (const Json& candidate : *voices) {
        if (candidate.is_object() && JsonString(candidate, "id") == voice_id) {
            *voice = candidate;
            return true;
        }
    }
    return false;
}

std::filesystem::path CloudTtsPipeline::RelativeChunkAudioPath(
    size_t chunk_index,
    const std::string& voice_id) {
    std::ostringstream filename;
    filename << "chunk_" << std::setw(6) << std::setfill('0') << chunk_index << ".wav";
    return std::filesystem::path("audio") / SafePathSegment(voice_id) / filename.str();
}

std::filesystem::path CloudTtsPipeline::ChunkAudioPath(
    const std::string& book_id,
    size_t chunk_index,
    const std::string& voice_id) {
    return BookDirectory(book_id) / RelativeChunkAudioPath(chunk_index, voice_id);
}

std::string CloudTtsPipeline::BuildBookId(
    const std::filesystem::path& source_file_path,
    const std::string& text) {
    std::string key = source_file_path.lexically_normal().string();
    if (key.empty() || key == "Untitled" || key == "untitled.txt") {
        key = text;
    }

    std::ostringstream output;
    output << "book_" << std::hex << std::setw(16) << std::setfill('0')
           << StableHash(key);
    return output.str();
}

CloudTtsPipeline::BookMetadata CloudTtsPipeline::BuildBookMetadata(
    const std::filesystem::path& source_file_path,
    const std::string& text,
    size_t current_cursor_line) {
    BookMetadata book;
    book.book_id = BuildBookId(source_file_path, text);
    book.source_file_path = source_file_path;
    book.file_name = source_file_path.filename().string();
    book.title = source_file_path.stem().string();
    book.last_cursor_line = current_cursor_line;

    std::error_code error;
    if (std::filesystem::is_regular_file(source_file_path, error)) {
        error.clear();
        book.file_size = std::filesystem::file_size(source_file_path, error);
        error.clear();
        const auto modified = std::filesystem::last_write_time(source_file_path, error);
        if (!error) {
            const auto system_time =
                std::chrono::time_point_cast<std::chrono::seconds>(
                    modified - decltype(modified)::clock::now() +
                    std::chrono::system_clock::now());
            book.modified_time = system_time.time_since_epoch().count();
        }
    } else {
        book.file_size = text.size();
    }

    return book;
}

bool CloudTtsPipeline::LoadBookInfo(
    const std::filesystem::path& book_directory,
    BookInfo* book) {
    const Json book_json = LoadJsonObject(book_directory / "book.json");
    if (book_json.empty()) {
        return false;
    }

    BookInfo loaded;
    loaded.book_id = JsonString(book_json, "book_id");
    if (loaded.book_id.empty()) {
        loaded.book_id = book_directory.filename().string();
    }
    if (loaded.book_id.empty()) {
        return false;
    }

    loaded.title = JsonString(book_json, "title");
    loaded.author = JsonString(book_json, "author");
    loaded.series = JsonString(book_json, "series");
    loaded.genre = JsonString(book_json, "genre");
    loaded.series_index = JsonInt(book_json, "series_index", 0);
    loaded.language = JsonString(book_json, "language");
    loaded.piper_voice_id = JsonString(book_json, "piper_voice_id");
    loaded.source_file_name = JsonString(book_json, "file_name");
    loaded.source_path = JsonString(book_json, "source_file_path");
    loaded.file_size = static_cast<uintmax_t>(JsonSize(book_json, "file_size", 0));
    const auto modified = book_json.find("modified_time");
    if (modified != book_json.end() && modified->is_number_integer()) {
        loaded.modified_time = modified->get<long long>();
    }
    loaded.last_cursor_line = JsonSize(book_json, "last_cursor_line", 0);

    std::ifstream chunks_file(book_directory / "chunks.json", std::ios::binary);
    if (!chunks_file) {
        return false;
    }
    const Json chunks_json = Json::parse(chunks_file, nullptr, false);
    if (chunks_json.is_discarded() || !chunks_json.is_array()) {
        return false;
    }

    loaded.total_chunks = chunks_json.size();
    for (const Json& chunk : chunks_json) {
        if (!chunk.is_object()) {
            continue;
        }
        const std::string status = JsonString(chunk, "status");
        if (status == "prepared") {
            ++loaded.prepared_chunks;
        } else if (status == "ready") {
            ++loaded.ready_chunks;
        } else if (status == "failed") {
            ++loaded.failed_chunks;
        } else if (status == "played") {
            ++loaded.played_chunks;
        }
    }

    if (loaded.total_chunks > 0) {
        const size_t complete_chunks = loaded.ready_chunks + loaded.played_chunks;
        loaded.progress_ratio =
            static_cast<double>(complete_chunks) / static_cast<double>(loaded.total_chunks);
    }

    *book = std::move(loaded);
    return true;
}

bool CloudTtsPipeline::WriteBook(
    const BookMetadata& book,
    const std::vector<PreparedChunk>& chunks) {
    const std::filesystem::path book_directory = BookDirectory(book.book_id);
    if (book_directory.empty()) {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(book_directory / "audio", error);
    if (error) {
        return false;
    }

    const Json book_json = {
        {"book_id", book.book_id},
        {"source_file_path", book.source_file_path.string()},
        {"file_name", book.file_name},
        {"file_size", book.file_size},
        {"modified_time", book.modified_time},
        {"title", book.title},
        {"author", book.author},
        {"series", book.series},
        {"genre", book.genre},
        {"series_index", book.series_index},
        {"language", book.language},
        {"piper_voice_id", book.piper_voice_id},
        {"last_cursor_line", book.last_cursor_line},
    };

    Json chunks_json = Json::array();
    for (const PreparedChunk& chunk : chunks) {
        chunks_json.push_back({
            {"chunk_index", chunk.chunk_index},
            {"start_line", chunk.start_line},
            {"start_column", chunk.start_column},
            {"end_line", chunk.end_line},
            {"end_column", chunk.end_column},
            {"raw_size", chunk.raw_size_bytes},
            {"cleansed_text", chunk.cleansed_text},
            {"status", chunk.status},
            {"audio_path", chunk.audio_path},
        });
    }

    return WriteJsonAtomically(book_directory / "book.json", book_json) &&
           WriteJsonAtomically(book_directory / "chunks.json", chunks_json);
}

void CloudTtsPipeline::JoinWorker() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

} // namespace textlt
