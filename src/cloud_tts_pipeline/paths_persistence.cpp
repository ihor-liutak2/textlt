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
