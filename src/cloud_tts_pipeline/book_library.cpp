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
        const bool installed = PiperManager::VoiceInstalled(voice);
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
