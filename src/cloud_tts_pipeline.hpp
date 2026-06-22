#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "json_utils.hpp"

namespace textlt {

class CloudTtsPipeline {
public:
    struct SourcePosition {
        size_t line = 0;
        size_t column = 0;
    };

    struct PreparedChunk {
        size_t chunk_index = 0;
        size_t start_line = 0;
        size_t start_column = 0;
        size_t end_line = 0;
        size_t end_column = 0;
        size_t raw_size_bytes = 0;
        std::string cleansed_text;
        std::string status;
        std::string audio_path;
    };

    struct BookMetadata {
        std::string book_id;
        std::filesystem::path source_file_path;
        std::string file_name;
        uintmax_t file_size = 0;
        long long modified_time = 0;
        std::string title;
        std::string author;
        std::string series;
        std::string genre;
        int series_index = 0;
        std::string language = "unknown";
        std::string piper_voice_id;
        size_t last_cursor_line = 0;
    };

    struct BookInfo {
        std::string book_id;
        std::string title;
        std::string author;
        std::string series;
        std::string genre;
        int series_index = 0;
        std::string language = "unknown";
        std::string piper_voice_id;
        std::string source_file_name;
        std::filesystem::path source_path;
        uintmax_t file_size = 0;
        long long modified_time = 0;
        size_t last_cursor_line = 0;
        size_t total_chunks = 0;
        size_t prepared_chunks = 0;
        size_t ready_chunks = 0;
        size_t failed_chunks = 0;
        size_t played_chunks = 0;
        double progress_ratio = 0.0;
    };

    struct EditableBookMetadata {
        std::string title;
        std::string author;
        std::string series;
        std::string genre;
        int series_index = 0;
        std::string language = "unknown";
        std::string piper_voice_id;
    };

    struct MetadataSuggestions {
        std::vector<std::string> series;
        std::vector<std::string> genres;
    };

    struct LanguageOption {
        std::string code;
        std::string label;
    };

    struct PiperVoiceOption {
        std::string id;
        std::string label;
        bool installed = false;
    };

    CloudTtsPipeline();
    ~CloudTtsPipeline();

    CloudTtsPipeline(const CloudTtsPipeline&) = delete;
    CloudTtsPipeline& operator=(const CloudTtsPipeline&) = delete;

    void Submit(
        std::string entire_document_text,
        std::filesystem::path source_file_path,
        size_t current_cursor_line);
    std::vector<BookInfo> ListLocalBooks() const;
    bool LoadBookMetadata(const std::string& book_id, BookMetadata* book) const;
    bool UpdateBookMetadata(
        const std::string& book_id,
        const EditableBookMetadata& metadata) const;
    MetadataSuggestions BuildMetadataSuggestions(
        const std::vector<BookInfo>& books) const;
    std::vector<LanguageOption> ListLanguageOptions() const;
    std::vector<PiperVoiceOption> ListPiperVoiceOptions(
        const std::string& language_code) const;

private:
    static std::vector<PreparedChunk> BuildPreparedChunks(
        const std::string& text,
        SourcePosition start_position);
    static std::vector<std::string> BuildRawChunks(const std::string& text);
    static std::vector<std::string> SplitParagraphs(const std::string& text);
    static std::vector<std::string> SliceOversizedParagraph(const std::string& paragraph);
    static size_t FindSliceEnd(const std::string& text, size_t start);
    static SourcePosition AdvancePosition(SourcePosition position, const std::string& text);
    static std::string CleanseText(const std::string& text);
    static std::filesystem::path UserDataDirectory();
    static std::filesystem::path LibraryDirectory();
    static std::filesystem::path BookDirectory(const std::string& book_id);
    static std::filesystem::path RegistryDirectory();
    static bool PiperVoiceInstalled(const Json& voice);
    static std::string BuildBookId(
        const std::filesystem::path& source_file_path,
        const std::string& text);
    static BookMetadata BuildBookMetadata(
        const std::filesystem::path& source_file_path,
        const std::string& text,
        size_t current_cursor_line);
    static bool LoadBookInfo(const std::filesystem::path& book_directory, BookInfo* book);
    static bool WriteBook(const BookMetadata& book,
                          const std::vector<PreparedChunk>& chunks);
    void JoinWorker();

    std::thread worker_;
};

} // namespace textlt
