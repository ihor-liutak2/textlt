/* Included by ../modal_tts.cpp. */

void TtsModalContent::StartRunWorkflow(bool force_rebuild, bool play_after) {
    if (!prepare_current_file_ || !pipeline_) {
        status_ = "Current document is not available to TTS UI";
        return;
    }
    {
        std::lock_guard<std::mutex> lock(audio_worker_mutex_);
        if (audio_worker_running_) {
            status_ = "TTS preparation is already running";
            return;
        }
    }
    JoinAudioWorker();
    {
        std::lock_guard<std::mutex> lock(audio_worker_mutex_);
        audio_worker_running_ = true;
        audio_worker_play_after_ = play_after;
        audio_worker_status_ = "Preparing current file...";
        audio_worker_frame_ = 0;
        status_ = audio_worker_status_;
    }
    NotifyUiRefresh();
    prepare_current_file_(force_rebuild);

    audio_worker_ = std::thread([this] {
        const auto started_at = std::chrono::steady_clock::now();
        auto future = std::async(std::launch::async, [this] {
            std::string final_status;
            const std::string book_id = pipeline_->WaitForPendingPreparation();
            if (book_id.empty()) {
                return std::make_pair(book_id, std::string("TTS book preparation failed"));
            }
            CloudTtsPipeline::BookMetadata book;
            if (!pipeline_->LoadBookMetadata(book_id, &book)) {
                return std::make_pair(book_id, std::string("Prepared book metadata is unavailable"));
            }
            if (book.piper_voice_id.empty()) {
                return std::make_pair(book_id, std::string("Select and save an installed voice first"));
            }
            const size_t chunk = pipeline_->FindChunkIndexForLine(book_id, book.last_cursor_line);
            std::string error;
            const auto result = pipeline_->EnsureAudioLookahead(
                book_id, chunk, book.piper_voice_id, 3, &error);
            std::ostringstream message;
            message << "Prepared current chunk + next 2: "
                    << result.generated_chunks << " generated, "
                    << result.already_ready_chunks << " ready";
            if (result.failed_chunks > 0) {
                message << ", " << result.failed_chunks << " failed";
                if (!error.empty()) {
                    message << " - " << error;
                }
            }
            return std::make_pair(book_id, message.str());
        });
        while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
            ++audio_worker_frame_;
            NotifyUiRefresh();
        }
        while (std::chrono::steady_clock::now() - started_at <
               std::chrono::milliseconds(300)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ++audio_worker_frame_;
            NotifyUiRefresh();
        }
        auto result = future.get();
        {
            std::lock_guard<std::mutex> lock(audio_worker_mutex_);
            audio_worker_running_ = false;
            audio_worker_selected_book_id_ = std::move(result.first);
            audio_worker_status_ = std::move(result.second);
            audio_worker_refresh_pending_ = true;
        }
        NotifyUiRefresh();
    });
}

void TtsModalContent::RefreshLibrary() {
    const std::string selected_book_id =
        !library_books_.empty() &&
                selected_library_book_ >= 0 &&
                selected_library_book_ < static_cast<int>(library_books_.size())
            ? library_books_[selected_library_book_].book_id
            : "";

    library_books_.clear();
    library_book_labels_.clear();
    selected_library_book_ = 0;

    if (!pipeline_) {
        library_book_labels_.push_back("No prepared books");
        status_ = "TTS pipeline is not available";
        SyncMetadataFieldsFromSelection();
        return;
    }

    try {
        library_books_ = pipeline_->ListLocalBooks();
    } catch (const std::exception& exception) {
        library_book_labels_.push_back("No prepared books");
        status_ = std::string("TTS library load failed: ") + exception.what();
        SyncMetadataFieldsFromSelection();
        return;
    } catch (...) {
        library_book_labels_.push_back("No prepared books");
        status_ = "TTS library load failed";
        SyncMetadataFieldsFromSelection();
        return;
    }

    for (const CloudTtsPipeline::BookInfo& book : library_books_) {
        library_book_labels_.push_back(BookListLabel(book));
    }
    RebuildMetadataOptions();
    if (!selected_book_id.empty()) {
        for (size_t index = 0; index < library_books_.size(); ++index) {
            if (library_books_[index].book_id == selected_book_id) {
                selected_library_book_ = static_cast<int>(index);
                break;
            }
        }
    }

    if (library_book_labels_.empty()) {
        library_book_labels_.push_back("No prepared books");
        status_ = "No prepared books";
    } else {
        status_ = "Loaded " + std::to_string(library_books_.size()) + " prepared books";
    }
    SyncMetadataFieldsFromSelection();
}

void TtsModalContent::RebuildMetadataOptions() {
    if (pipeline_) {
        const CloudTtsPipeline::MetadataSuggestions suggestions =
            pipeline_->BuildMetadataSuggestions(library_books_);
        known_series_ = suggestions.series;
        known_genres_ = suggestions.genres;
    } else {
        known_series_.clear();
        known_genres_.clear();
    }

    language_codes_ = {"unknown"};
    language_labels_ = {"Unknown"};
    if (!pipeline_) {
        return;
    }

    const std::vector<CloudTtsPipeline::LanguageOption> languages =
        pipeline_->ListLanguageOptions();
    language_codes_.clear();
    language_labels_.clear();
    for (const CloudTtsPipeline::LanguageOption& language : languages) {
        language_codes_.push_back(language.code);
        language_labels_.push_back(language.label);
    }
    if (language_codes_.empty()) {
        language_codes_ = {"unknown"};
        language_labels_ = {"Unknown"};
    }
}

void TtsModalContent::RebuildVoiceOptions() {
    piper_voice_ids_.clear();
    piper_voice_labels_.clear();
    piper_voice_installed_.clear();
    selected_piper_voice_ = 0;

    const std::string language =
        selected_language_ >= 0 &&
                selected_language_ < static_cast<int>(language_codes_.size())
            ? language_codes_[selected_language_]
            : "unknown";
    if (language.empty() || language == "unknown") {
        piper_voice_labels_.push_back("Select book language first");
        return;
    }
    if (!pipeline_) {
        piper_voice_labels_.push_back("TTS pipeline is not available");
        return;
    }

    const std::vector<CloudTtsPipeline::PiperVoiceOption> voices =
        pipeline_->ListPiperVoiceOptions(language);
    for (const CloudTtsPipeline::PiperVoiceOption& voice : voices) {
        piper_voice_ids_.push_back(voice.id);
        piper_voice_labels_.push_back(voice.label);
        piper_voice_installed_.push_back(voice.installed);
    }

    if (piper_voice_labels_.empty()) {
        piper_voice_labels_.push_back("No Piper voices for selected language");
        return;
    }
    AutoSelectPreferredVoice();
}

void TtsModalContent::AutoSelectPreferredVoice() {
    auto find_installed_quality = [this](const std::string& quality) {
        for (size_t index = 0; index < piper_voice_ids_.size(); ++index) {
            if (index < piper_voice_installed_.size() && piper_voice_installed_[index] &&
                (LowerAscii(piper_voice_ids_[index]).find(quality) != std::string::npos ||
                 LowerAscii(piper_voice_labels_[index]).find(quality) != std::string::npos)) {
                return static_cast<int>(index);
            }
        }
        return -1;
    };
    int preferred = find_installed_quality("medium");
    if (preferred < 0) {
        preferred = find_installed_quality("high");
    }
    selected_piper_voice_ = preferred < 0 ? 0 : preferred;
}

void TtsModalContent::SyncMetadataFieldsFromSelection() {
    if (library_books_.empty() ||
        selected_library_book_ < 0 ||
        selected_library_book_ >= static_cast<int>(library_books_.size())) {
        metadata_title_.clear();
        metadata_author_.clear();
        metadata_series_.clear();
        metadata_genre_.clear();
        metadata_series_index_.clear();
        selected_language_ = 0;
        RebuildVoiceOptions();
        show_selected_book_info_ = false;
        info_layer_index_ = 0;
        return;
    }

    show_selected_book_info_ = false;
    info_layer_index_ = 0;
    const CloudTtsPipeline::BookInfo& book = library_books_[selected_library_book_];
    metadata_title_ = book.title;
    metadata_author_ = book.author;
    metadata_series_ = book.series;
    metadata_genre_ = book.genre;
    metadata_series_index_ = std::to_string(book.series_index);
    selected_language_ = 0;
    const std::string language = book.language.empty() ? "unknown" : book.language;
    for (size_t index = 0; index < language_codes_.size(); ++index) {
        if (language_codes_[index] == language) {
            selected_language_ = static_cast<int>(index);
            break;
        }
    }
    RebuildVoiceOptions();
    bool saved_voice_available = false;
    if (!book.piper_voice_id.empty()) {
        for (size_t index = 0; index < piper_voice_ids_.size(); ++index) {
            if (piper_voice_ids_[index] == book.piper_voice_id) {
                selected_piper_voice_ = static_cast<int>(index);
                saved_voice_available = true;
                break;
            }
        }
    }
    if (!saved_voice_available && pipeline_ &&
        selected_piper_voice_ >= 0 &&
        selected_piper_voice_ < static_cast<int>(piper_voice_ids_.size()) &&
        selected_piper_voice_ < static_cast<int>(piper_voice_installed_.size()) &&
        piper_voice_installed_[selected_piper_voice_]) {
        CloudTtsPipeline::EditableBookMetadata metadata;
        metadata.title = book.title;
        metadata.author = book.author;
        metadata.series = book.series;
        metadata.genre = book.genre;
        metadata.series_index = book.series_index;
        metadata.language = language;
        metadata.piper_voice_id = piper_voice_ids_[selected_piper_voice_];
        if (pipeline_->UpdateBookMetadata(book.book_id, metadata)) {
            library_books_[selected_library_book_].piper_voice_id =
                metadata.piper_voice_id;
        }
    }
}

void TtsModalContent::SaveSelectedMetadata() {
    if (!pipeline_) {
        status_ = "TTS pipeline is not available";
        return;
    }
    if (library_books_.empty() ||
        selected_library_book_ < 0 ||
        selected_library_book_ >= static_cast<int>(library_books_.size())) {
        status_ = "No prepared book selected";
        return;
    }

    int series_index = 0;
    if (!metadata_series_index_.empty()) {
        try {
            size_t parsed = 0;
            series_index = std::stoi(metadata_series_index_, &parsed);
            if (parsed != metadata_series_index_.size()) {
                status_ = "Series index must be a number";
                return;
            }
        } catch (...) {
            status_ = "Series index must be a number";
            return;
        }
    }

    const std::string book_id = library_books_[selected_library_book_].book_id;
    CloudTtsPipeline::EditableBookMetadata metadata;
    metadata.title = metadata_title_;
    metadata.author = metadata_author_;
    metadata.series = metadata_series_;
    metadata.genre = metadata_genre_;
    metadata.series_index = series_index;
    metadata.language =
        selected_language_ >= 0 &&
                selected_language_ < static_cast<int>(language_codes_.size())
            ? language_codes_[selected_language_]
            : "unknown";
    metadata.piper_voice_id =
        selected_piper_voice_ >= 0 &&
                selected_piper_voice_ < static_cast<int>(piper_voice_ids_.size())
            ? piper_voice_ids_[selected_piper_voice_]
            : "";

    if (!pipeline_->UpdateBookMetadata(book_id, metadata)) {
        status_ = "Save metadata failed";
        return;
    }

    RefreshLibrary();
    status_ = "Metadata saved";
}

void TtsModalContent::SaveSelectedVoice() {
    SaveSelectedMetadata();
    if (status_ == "Metadata saved") {
        status_ = "Voice saved";
    }
}


std::string TtsModalContent::SelectedBookId() const {
    if (library_books_.empty() ||
        selected_library_book_ < 0 ||
        selected_library_book_ >= static_cast<int>(library_books_.size())) {
        return {};
    }
    return library_books_[selected_library_book_].book_id;
}

std::string TtsModalContent::SelectedVoiceId() const {
    if (selected_piper_voice_ >= 0 &&
        selected_piper_voice_ < static_cast<int>(piper_voice_ids_.size())) {
        return piper_voice_ids_[selected_piper_voice_];
    }
    if (!library_books_.empty() &&
        selected_library_book_ >= 0 &&
        selected_library_book_ < static_cast<int>(library_books_.size())) {
        return library_books_[selected_library_book_].piper_voice_id;
    }
    return {};
}

size_t TtsModalContent::SelectedStartChunkIndex() const {
    if (!pipeline_ || library_books_.empty() ||
        selected_library_book_ < 0 ||
        selected_library_book_ >= static_cast<int>(library_books_.size())) {
        return 0;
    }
    const CloudTtsPipeline::BookInfo& book = library_books_[selected_library_book_];
    return pipeline_->FindChunkIndexForLine(book.book_id, book.last_cursor_line);
}

std::string TtsModalContent::SelectedAudioCacheSizeText() const {
    if (!pipeline_) {
        return "0 bytes";
    }
    const std::string book_id = SelectedBookId();
    if (book_id.empty()) {
        return "0 bytes";
    }
    return FormatHumanBytes(static_cast<unsigned long long>(pipeline_->BookAudioCacheSize(book_id)));
}

void TtsModalContent::JoinAudioWorker() {
    if (audio_worker_.joinable()) {
        audio_worker_.join();
    }
}

void TtsModalContent::ApplyAudioWorkerState() {
    bool should_refresh = false;
    bool play_after = false;
    std::string worker_status;
    std::string selected_book_id;
    {
        std::lock_guard<std::mutex> lock(audio_worker_mutex_);
        if (!audio_worker_refresh_pending_) {
            return;
        }
        audio_worker_refresh_pending_ = false;
        should_refresh = true;
        play_after = audio_worker_play_after_;
        audio_worker_play_after_ = false;
        worker_status = audio_worker_status_;
        selected_book_id = std::move(audio_worker_selected_book_id_);
    }

    if (should_refresh) {
        RefreshLibrary();
        if (!selected_book_id.empty()) {
            for (size_t index = 0; index < library_books_.size(); ++index) {
                if (library_books_[index].book_id == selected_book_id) {
                    selected_library_book_ = static_cast<int>(index);
                    SyncMetadataFieldsFromSelection();
                    break;
                }
            }
        }
        if (!worker_status.empty()) {
            status_ = worker_status;
        }
        if (play_after && !selected_book_id.empty()) {
            StartPlaybackFrom(SelectedStartChunkIndex(), true);
        }
    }
}

void TtsModalContent::TestCurrentChunk() {
    if (!pipeline_) {
        status_ = "TTS pipeline is not available";
        return;
    }

    if (set_header_button_active_) {
        set_header_button_active_(TtsHeaderButton::Test);
    }

    const std::string book_id = SelectedBookId();
    if (book_id.empty()) {
        status_ = "No prepared book selected";
        return;
    }

    const size_t chunk_index = SelectedStartChunkIndex();
    test_chunk_index_ = chunk_index;
    test_text_ = pipeline_->GetChunkText(book_id, chunk_index);
    if (test_text_.empty()) {
        test_text_ = "No text available for chunk " + std::to_string(chunk_index + 1);
    }

    info_popup_mode_ = InfoPopupMode::TestText;
    info_popup_pending_ = true;
    NotifyUiRefresh();

    StartPlaybackFrom(chunk_index, true);
}
