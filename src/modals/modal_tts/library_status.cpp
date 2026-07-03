/* Included by ../modal_tts.cpp. */

void TtsModalContent::ClearSelectedAudioCache() {
    if (!pipeline_) {
        status_ = "TTS pipeline is not available";
        return;
    }
    const std::string book_id = SelectedBookId();
    if (book_id.empty()) {
        status_ = "No prepared book selected";
        return;
    }
    {
        std::lock_guard<std::mutex> lock(audio_worker_mutex_);
        if (audio_worker_running_) {
            status_ = "Audio generation is running";
            return;
        }
    }

    std::string error;
    if (!pipeline_->ClearBookAudioCache(book_id, &error)) {
        status_ = error.empty() ? "Clear audio cache failed" : "Clear audio cache failed: " + error;
        return;
    }
    RefreshLibrary();
    status_ = "Audio cache cleared";
}

void TtsModalContent::ShowSelectedBookInfo() {
    if (library_books_.empty() ||
        selected_library_book_ < 0 ||
        selected_library_book_ >= static_cast<int>(library_books_.size())) {
        show_selected_book_info_ = false;
        status_ = "No prepared book selected";
        return;
    }
    info_popup_pending_ = true;
    status_ = "Opening book info";
    NotifyUiRefresh();
}

void TtsModalContent::CloseSelectedBookInfo() {
    show_selected_book_info_ = false;
    info_popup_pending_ = false;
    info_layer_index_ = 0;
    if (library_book_menu_) {
        library_book_menu_->TakeFocus();
    }
    status_ = "Book info closed";
}

void TtsModalContent::DeleteSelectedBook() {
    if (!pipeline_) {
        status_ = "TTS pipeline is not available";
        return;
    }
    const std::string book_id = SelectedBookId();
    if (book_id.empty()) {
        status_ = "No prepared book selected";
        return;
    }
    {
        std::lock_guard<std::mutex> lock(audio_worker_mutex_);
        if (audio_worker_running_) {
            status_ = "Stop audio generation before deleting the book";
            return;
        }
    }

    Stop();
    JoinPlaybackWorker();
    std::string error;
    if (!pipeline_->DeleteBook(book_id, &error)) {
        status_ = error.empty() ? "Delete book failed" : "Delete book failed: " + error;
        return;
    }
    show_selected_book_info_ = false;
    info_layer_index_ = 0;
    RefreshLibrary();
    status_ = "Book and all TTS files deleted";
}

bool TtsModalContent::IsAudioWorkerRunning() const {
    std::lock_guard<std::mutex> lock(audio_worker_mutex_);
    return audio_worker_running_;
}

bool TtsModalContent::IsPlaybackActive() const {
    std::lock_guard<std::mutex> lock(playback_mutex_);
    return playback_worker_running_ || playback_paused_;
}

bool TtsModalContent::HasPreparedBook() const {
    return !library_books_.empty() &&
           selected_library_book_ >= 0 &&
           selected_library_book_ < static_cast<int>(library_books_.size());
}

std::string TtsModalContent::HeaderStatus() const {
    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        if (playback_worker_running_ || playback_paused_) {
            return playback_status_.empty() ? "Playback ready" : playback_status_;
        }
    }
    {
        std::lock_guard<std::mutex> lock(audio_worker_mutex_);
        if (audio_worker_running_) {
            return audio_worker_status_.empty() ? "Generating audio" : audio_worker_status_;
        }
    }

    if (!HasPreparedBook()) {
        return {};
    }

    const CloudTtsPipeline::BookInfo& book = library_books_[selected_library_book_];
    std::string title = BookDisplayTitle(book);
    if (title.size() > 28) {
        title = title.substr(0, 25) + "...";
    }
    return title + " " + std::to_string(book.ready_chunks) + "/" +
           std::to_string(book.prepared_chunks) + " ready";
}

std::string TtsModalContent::GetFooterText() const {
    std::lock_guard<std::mutex> lock(playback_mutex_);
    if (playback_worker_running_ || playback_paused_ ||
        (!playback_status_.empty() && playback_status_ != "Playback stopped")) {
        return playback_status_;
    }
    return status_;
}
