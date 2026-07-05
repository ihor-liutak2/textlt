bool CloudTtsPipeline::PiperRuntimeInstalled() const {
    return PiperManager::RuntimeInstalled();
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

    if (!PiperManager::RuntimeInstalled()) {
        if (error) {
            *error = "Piper runtime is not installed";
        }
        return false;
    }

    Json voice;
    if (!PiperManager::FindVoiceById(voice_id, &voice)) {
        if (error) {
            *error = "Piper voice is not in registry";
        }
        return false;
    }
    if (!PiperManager::VoiceInstalled(voice)) {
        if (error) {
            *error = "Piper voice files are missing";
        }
        return false;
    }

    const std::filesystem::path book_directory = BookDirectory(book_id);
    const std::filesystem::path chunks_path = book_directory / "chunks.json";
    Json chunks_json = LoadJsonValue(chunks_path);
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

    std::string generation_error;
    const bool ok = PiperManager::RunToFile(voice, text, output_wav, &generation_error);

    exists_error.clear();
    const bool output_exists = std::filesystem::exists(output_wav, exists_error);
    if (!ok || !output_exists) {
        chunk["status"] = "failed";
        chunk["audio_path"] = "";
        WriteJsonAtomically(chunks_path, chunks_json);
        if (error) {
            *error = generation_error.empty() ? "Piper generation failed" : generation_error;
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
    const Json chunks_json = LoadJsonValue(chunks_path);
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
    Json chunks_json = LoadJsonValue(chunks_path);
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

bool CloudTtsPipeline::DeleteBook(const std::string& book_id, std::string* error) const {
    if (book_id.empty() || SafePathSegment(book_id) != book_id ||
        book_id.rfind("book_", 0) != 0) {
        if (error) {
            *error = "Invalid book id";
        }
        return false;
    }

    BookMetadata metadata;
    if (!LoadBookMetadata(book_id, &metadata) || metadata.book_id != book_id) {
        if (error) {
            *error = "Book metadata is missing or invalid";
        }
        return false;
    }

    const std::filesystem::path book_directory = BookDirectory(book_id);
    std::error_code remove_error;
    const uintmax_t removed = std::filesystem::remove_all(book_directory, remove_error);
    if (remove_error || removed == 0) {
        if (error) {
            *error = remove_error ? "Cannot delete book files" : "Book files were not found";
        }
        return false;
    }
    return true;
}


bool CloudTtsPipeline::GetChunkAudioPath(
    const std::string& book_id,
    size_t chunk_index,
    const std::string& voice_id,
    std::filesystem::path* audio_path,
    std::string* error) const {
    if (!audio_path) {
        if (error) {
            *error = "Audio output path target is missing";
        }
        return false;
    }
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

    const std::filesystem::path chunks_path = BookDirectory(book_id) / "chunks.json";
    const Json chunks_json = LoadJsonValue(chunks_path);
    if (!chunks_json.is_array() || chunk_index >= chunks_json.size()) {
        if (error) {
            *error = "Chunk index is out of range";
        }
        return false;
    }

    const Json& chunk = chunks_json[chunk_index];
    std::error_code exists_error;
    const std::filesystem::path selected_voice_audio = ChunkAudioPath(book_id, chunk_index, voice_id);
    if (std::filesystem::exists(selected_voice_audio, exists_error)) {
        *audio_path = selected_voice_audio;
        return true;
    }

    const std::string stored_audio_path = JsonString(chunk, "audio_path");
    if (!stored_audio_path.empty()) {
        exists_error.clear();
        const std::filesystem::path stored_candidate =
            BookDirectory(book_id) / std::filesystem::path(stored_audio_path);
        if (std::filesystem::exists(stored_candidate, exists_error)) {
            *audio_path = stored_candidate;
            return true;
        }
    }

    if (error) {
        *error = "Chunk audio has not been generated yet";
    }
    return false;
}

bool CloudTtsPipeline::MarkChunkPlayed(
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

    const std::filesystem::path chunks_path = BookDirectory(book_id) / "chunks.json";
    Json chunks_json = LoadJsonValue(chunks_path);
    if (!chunks_json.is_array() || chunk_index >= chunks_json.size()) {
        if (error) {
            *error = "Chunk index is out of range";
        }
        return false;
    }

    std::filesystem::path audio_path;
    std::string audio_error;
    if (!GetChunkAudioPath(book_id, chunk_index, voice_id, &audio_path, &audio_error)) {
        if (error) {
            *error = audio_error.empty() ? "Chunk audio is missing" : audio_error;
        }
        return false;
    }

    Json& chunk = chunks_json[chunk_index];
    chunk["status"] = "played";
    if (JsonString(chunk, "audio_path").empty()) {
        chunk["audio_path"] = RelativeChunkAudioPath(chunk_index, voice_id).generic_string();
    }
    return WriteJsonAtomically(chunks_path, chunks_json);
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
    const Json chunks_json = LoadJsonValue(BookDirectory(book_id) / "chunks.json");
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
        const bool contains_line = (end_line > start_line)
            ? (line >= start_line && line < end_line)
            : (line >= start_line && line <= end_line);
        if (contains_line) {
            return index;
        }
    }
    return 0;
}

std::string CloudTtsPipeline::GetChunkText(const std::string& book_id, size_t chunk_index) const {
    const std::filesystem::path chunks_path = BookDirectory(book_id) / "chunks.json";
    if (!std::filesystem::exists(chunks_path)) {
        return "";
    }
    const Json chunks_json = LoadJsonValue(chunks_path);
    if (!chunks_json.is_array() || chunk_index >= chunks_json.size()) {
        return "";
    }
    const Json& chunk = chunks_json[chunk_index];
    if (!chunk.is_object()) {
        return "";
    }
    return JsonString(chunk, "cleansed_text");
}
