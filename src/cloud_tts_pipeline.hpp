#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace textlt {

class CloudTtsPipeline {
public:
    struct SourcePosition {
        size_t line = 0;
        size_t column = 0;
    };

    struct ChunkDiagnostic {
        size_t chunk_index = 0;
        size_t start_line = 0;
        size_t start_column = 0;
        size_t end_line = 0;
        size_t end_column = 0;
        size_t raw_size_bytes = 0;
        std::string cleansed_text;
        std::string network_status;
    };

    CloudTtsPipeline();
    ~CloudTtsPipeline();

    CloudTtsPipeline(const CloudTtsPipeline&) = delete;
    CloudTtsPipeline& operator=(const CloudTtsPipeline&) = delete;

    void Submit(std::string entire_document_text, size_t current_cursor_line);

private:
    static std::vector<ChunkDiagnostic> BuildDiagnostics(
        const std::string& text,
        SourcePosition start_position);
    static std::vector<std::string> BuildRawChunks(const std::string& text);
    static std::vector<std::string> SplitParagraphs(const std::string& text);
    static std::vector<std::string> SliceOversizedParagraph(const std::string& paragraph);
    static size_t FindSliceEnd(const std::string& text, size_t start);
    static SourcePosition AdvancePosition(SourcePosition position, const std::string& text);
    static std::string CleanseText(const std::string& text);
    static std::filesystem::path DiagnosticPath();
    static void WriteDiagnostics(const std::vector<ChunkDiagnostic>& diagnostics);
    void JoinWorker();

    std::thread worker_;
};

} // namespace textlt
