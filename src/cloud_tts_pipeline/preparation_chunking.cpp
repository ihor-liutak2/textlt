void CloudTtsPipeline::Submit(
    std::string entire_document_text,
    std::filesystem::path source_file_path,
    size_t current_cursor_line,
    bool force_rebuild) {
    JoinWorker();

    const BookMetadata requested =
        BuildBookMetadata(source_file_path, entire_document_text, current_cursor_line);
    BookMetadata existing;
    const std::filesystem::path existing_book_path =
        BookDirectory(requested.book_id) / "book.json";
    const std::filesystem::path existing_chunks_path =
        BookDirectory(requested.book_id) / "chunks.json";
    Json existing_chunks = LoadJsonValue(existing_chunks_path);
    if (!force_rebuild && LoadBookMetadata(requested.book_id, &existing) &&
        existing_chunks.is_array() && !existing_chunks.empty()) {
        Json book_json = LoadJsonObject(existing_book_path);
        book_json["last_cursor_line"] = current_cursor_line;
        WriteJsonAtomically(existing_book_path, book_json);
        std::lock_guard<std::mutex> lock(preparation_mutex_);
        last_prepared_book_id_ = requested.book_id;
        return;
    }

    worker_ = std::thread([this,
        text = std::move(entire_document_text),
        source_file_path = std::move(source_file_path),
        current_cursor_line] {
        // Chunk positions must always map to the full document, not to the
        // cursor position used to choose the initial TTS chunk.
        SourcePosition start_pos = {0, 0};
        BookMetadata book =
            BuildBookMetadata(source_file_path, text, current_cursor_line);
        BookMetadata existing;
        if (LoadBookMetadata(book.book_id, &existing)) {
            book.title = existing.title;
            book.author = existing.author;
            book.series = existing.series;
            book.genre = existing.genre;
            book.series_index = existing.series_index;
            book.language = existing.language;
            book.piper_voice_id = existing.piper_voice_id;
        }
        const bool written = WriteBook(book, BuildPreparedChunks(text, start_pos));
        std::lock_guard<std::mutex> lock(preparation_mutex_);
        last_prepared_book_id_ = written ? book.book_id : std::string{};
    });
}

std::string CloudTtsPipeline::WaitForPendingPreparation() {
    JoinWorker();
    std::lock_guard<std::mutex> lock(preparation_mutex_);
    return last_prepared_book_id_;
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
