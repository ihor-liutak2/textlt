#include "cloud_tts_pipeline.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "json_utils.hpp"

namespace {

std::filesystem::path TestDataRoot() {
    return std::filesystem::temp_directory_path() / "textlt_tts_pipeline_test_data";
}

void SetUserDataHome(const std::filesystem::path& root) {
#ifdef _WIN32
    _putenv_s("LOCALAPPDATA", root.string().c_str());
#else
    setenv("XDG_DATA_HOME", root.string().c_str(), 1);
#endif
}

std::string BuildLargeDocument() {
    std::string text;
    for (int line = 0; line < 140; ++line) {
        text += "This is a deterministic TTS test sentence for chunk mapping line ";
        text += std::to_string(line);
        text += ". It has enough text to cross chunk boundaries.\n";
    }
    return text;
}

textlt::Json LoadJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return textlt::Json();
    }
    return textlt::Json::parse(file, nullptr, false);
}

std::filesystem::path BookDirectory(const std::filesystem::path& root,
                                    const std::string& book_id) {
    return root / "textlt" / "tts" / "library" / "books" / book_id;
}

textlt::CloudTtsPipeline::BookInfo PrepareTestBook(const std::filesystem::path& root) {
    const std::filesystem::path source_path = root / "source-book.txt";
    const std::string text = BuildLargeDocument();
    {
        std::ofstream source(source_path, std::ios::binary | std::ios::trunc);
        source << text;
    }

    {
        textlt::CloudTtsPipeline pipeline;
        pipeline.Submit(text, source_path, 80);
    }

    textlt::CloudTtsPipeline pipeline;
    const auto books = pipeline.ListLocalBooks();
    assert(books.size() == 1);
    assert(books[0].last_cursor_line == 80);
    assert(books[0].total_chunks > 1);
    return books[0];
}

void TestChunkPositionsStartAtDocumentStart() {
    const std::filesystem::path root = TestDataRoot();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SetUserDataHome(root);

    const auto book = PrepareTestBook(root);
    const std::filesystem::path chunks_path = BookDirectory(root, book.book_id) / "chunks.json";
    const textlt::Json chunks = LoadJsonFile(chunks_path);
    assert(chunks.is_array());
    assert(chunks.size() > 1);

    assert(textlt::JsonSize(chunks[0], "start_line", 9999) == 0);
    assert(textlt::JsonSize(chunks[0], "start_column", 9999) == 0);

    textlt::CloudTtsPipeline pipeline;
    assert(pipeline.FindChunkIndexForLine(book.book_id, 0) == 0);

    const size_t second_start = textlt::JsonSize(chunks[1], "start_line", 0);
    assert(second_start > 0);
    assert(pipeline.FindChunkIndexForLine(book.book_id, second_start) == 1);
    assert(pipeline.FindChunkIndexForLine(book.book_id, book.last_cursor_line) < chunks.size());
}

void TestClearBookAudioCacheResetsChunks() {
    const std::filesystem::path root = TestDataRoot();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SetUserDataHome(root);

    const auto book = PrepareTestBook(root);
    const std::filesystem::path book_dir = BookDirectory(root, book.book_id);
    const std::filesystem::path audio_file = book_dir / "audio" / "chunk_0001" / "voice.wav";
    std::filesystem::create_directories(audio_file.parent_path());
    {
        std::ofstream wav(audio_file, std::ios::binary | std::ios::trunc);
        wav << "fake wav";
    }

    const std::filesystem::path chunks_path = book_dir / "chunks.json";
    textlt::Json chunks = LoadJsonFile(chunks_path);
    assert(chunks.is_array());
    assert(!chunks.empty());
    chunks[0]["status"] = "ready";
    chunks[0]["audio_path"] = "audio/chunk_0001/voice.wav";
    assert(textlt::WriteJsonAtomically(chunks_path, chunks));

    textlt::CloudTtsPipeline pipeline;
    assert(pipeline.BookAudioCacheSize(book.book_id) > 0);
    std::string error;
    assert(pipeline.ClearBookAudioCache(book.book_id, &error));
    assert(error.empty());
    assert(pipeline.BookAudioCacheSize(book.book_id) == 0);

    const textlt::Json reset_chunks = LoadJsonFile(chunks_path);
    assert(reset_chunks.is_array());
    assert(!reset_chunks.empty());
    assert(textlt::JsonString(reset_chunks[0], "status") == "prepared");
    assert(textlt::JsonString(reset_chunks[0], "audio_path") == "");
}

void TestDeleteBookRemovesOnlyTtsBookFiles() {
    const std::filesystem::path root = TestDataRoot();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SetUserDataHome(root);

    const auto book = PrepareTestBook(root);
    const std::filesystem::path book_dir = BookDirectory(root, book.book_id);
    const std::filesystem::path source_path = root / "source-book.txt";
    assert(std::filesystem::exists(book_dir / "book.json"));
    assert(std::filesystem::exists(book_dir / "chunks.json"));

    textlt::CloudTtsPipeline pipeline;
    std::string error;
    assert(pipeline.DeleteBook(book.book_id, &error));
    assert(error.empty());
    assert(!std::filesystem::exists(book_dir));
    assert(std::filesystem::exists(source_path));
    assert(pipeline.ListLocalBooks().empty());

    assert(!pipeline.DeleteBook("../source-book.txt", &error));
    assert(std::filesystem::exists(source_path));
}

void TestPreparationCompletionPreservesMetadata() {
    const std::filesystem::path root = TestDataRoot();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SetUserDataHome(root);

    const auto book = PrepareTestBook(root);
    textlt::CloudTtsPipeline pipeline;
    textlt::CloudTtsPipeline::EditableBookMetadata metadata;
    metadata.title = "Saved title";
    metadata.language = "en";
    metadata.piper_voice_id = "en_US-test-medium";
    assert(pipeline.UpdateBookMetadata(book.book_id, metadata));

    const std::filesystem::path source_path = root / "source-book.txt";
    pipeline.Submit(BuildLargeDocument(), source_path, 42);
    assert(pipeline.WaitForPendingPreparation() == book.book_id);

    textlt::CloudTtsPipeline::BookMetadata loaded;
    assert(pipeline.LoadBookMetadata(book.book_id, &loaded));
    assert(loaded.title == "Saved title");
    assert(loaded.language == "en");
    assert(loaded.piper_voice_id == "en_US-test-medium");
    assert(loaded.last_cursor_line == 42);

    const std::filesystem::path chunks_path =
        BookDirectory(root, book.book_id) / "chunks.json";
    const textlt::Json chunks_before = LoadJsonFile(chunks_path);
    pipeline.Submit("Completely different text that must not replace prepared chunks.",
                    source_path,
                    17);
    assert(pipeline.WaitForPendingPreparation() == book.book_id);
    assert(LoadJsonFile(chunks_path) == chunks_before);
    assert(pipeline.LoadBookMetadata(book.book_id, &loaded));
    assert(loaded.last_cursor_line == 17);

    pipeline.Submit("Forced refresh replaces the prepared chunks.",
                    source_path,
                    3,
                    true);
    assert(pipeline.WaitForPendingPreparation() == book.book_id);
    const textlt::Json refreshed_chunks = LoadJsonFile(chunks_path);
    assert(refreshed_chunks.is_array());
    assert(refreshed_chunks.size() == 1);
    assert(refreshed_chunks != chunks_before);
    assert(pipeline.LoadBookMetadata(book.book_id, &loaded));
    assert(loaded.title == "Saved title");
    assert(loaded.language == "en");
    assert(loaded.piper_voice_id == "en_US-test-medium");
    assert(loaded.last_cursor_line == 3);
}

void TestAudioLookaheadReusesExistingWav() {
    const std::filesystem::path root = TestDataRoot();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SetUserDataHome(root);

    const auto book = PrepareTestBook(root);
    const std::string voice_id = "en_US-test-medium";
    const std::filesystem::path wav_path =
        BookDirectory(root, book.book_id) /
        "audio" / voice_id / "chunk_000000.wav";
    std::filesystem::create_directories(wav_path.parent_path());
    {
        std::ofstream wav(wav_path, std::ios::binary | std::ios::trunc);
        wav << "existing audio";
    }

    textlt::CloudTtsPipeline pipeline;
    std::string error;
    const auto result = pipeline.EnsureAudioLookahead(
        book.book_id, 0, voice_id, 1, &error);
    assert(error.empty());
    assert(result.generated_chunks == 0);
    assert(result.already_ready_chunks == 1);
    std::ifstream wav(wav_path, std::ios::binary);
    assert(std::string(std::istreambuf_iterator<char>(wav), {}) == "existing audio");
}

} // namespace

int main() {
    TestChunkPositionsStartAtDocumentStart();
    TestClearBookAudioCacheResetsChunks();
    TestDeleteBookRemovesOnlyTtsBookFiles();
    TestPreparationCompletionPreservesMetadata();
    TestAudioLookaheadReusesExistingWav();
    std::filesystem::remove_all(TestDataRoot());
    std::cout << "TTS pipeline tests passed\n";
    return 0;
}
