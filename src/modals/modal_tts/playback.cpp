/* Included by ../modal_tts.cpp. */

void TtsModalContent::NotifyUiRefresh() {
    if (request_ui_refresh_) {
        request_ui_refresh_();
    }
}

void TtsModalContent::SetPlaybackStatus(std::string status) {
    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        playback_status_ = std::move(status);
    }
    NotifyUiRefresh();
}

void TtsModalContent::JoinPlaybackWorker() {
    if (playback_worker_.joinable()) {
        playback_worker_.join();
    }
}

void TtsModalContent::StartPlaybackFrom(size_t chunk_index, bool single_chunk) {
    if (!pipeline_) {
        status_ = "TTS pipeline is not available";
        return;
    }
    if (!TtsAudioPlayer::HasAvailablePlayer()) {
        status_ = TtsAudioPlayer::DependencyHelpText();
        return;
    }

    const std::string book_id = SelectedBookId();
    if (book_id.empty()) {
        status_ = "No prepared book selected";
        return;
    }
    const std::string voice_id = SelectedVoiceId();
    if (voice_id.empty()) {
        status_ = "No Piper voice selected";
        return;
    }
    if (!pipeline_->PiperRuntimeInstalled()) {
        status_ = "Piper runtime is not installed. Use Assistant Settings / TTS / Install Piper.";
        return;
    }
    if (selected_piper_voice_ >= 0 &&
        selected_piper_voice_ < static_cast<int>(piper_voice_installed_.size()) &&
        !piper_voice_installed_[selected_piper_voice_]) {
        status_ = "Selected voice is not installed. Use Assistant Settings / TTS to download it.";
        return;
    }
    if (library_books_.empty() ||
        selected_library_book_ < 0 ||
        selected_library_book_ >= static_cast<int>(library_books_.size())) {
        status_ = "No prepared book selected";
        return;
    }

    const CloudTtsPipeline::BookInfo& book = library_books_[selected_library_book_];
    if (book.total_chunks == 0) {
        status_ = "Selected book has no TTS chunks";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        if (playback_worker_running_) {
            status_ = "Playback is already running";
            return;
        }
    }

    JoinPlaybackWorker();
    playback_stop_requested_.store(false);
    playback_pause_requested_.store(false);
    playback_next_requested_.store(false);

    chunk_index = std::min(chunk_index, book.total_chunks - 1);
    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        playback_worker_running_ = true;
        playback_paused_ = false;
        playback_has_position_ = true;
        playback_chunk_index_ = chunk_index;
        playback_status_ = "Starting playback";
        status_ = "Starting playback";
    }

    playback_worker_ = std::thread(
        [this, book_id, voice_id, chunk_index, total_chunks = book.total_chunks, single_chunk] {
            PlaybackLoop(book_id, voice_id, chunk_index, total_chunks, single_chunk);
        });
    NotifyUiRefresh();
}

void TtsModalContent::PlaybackLoop(
    std::string book_id,
    std::string voice_id,
    size_t start_chunk_index,
    size_t total_chunks,
    bool single_chunk) {
    size_t index = start_chunk_index;
    std::string final_status;

    while (index < total_chunks) {
        if (playback_stop_requested_.load()) {
            break;
        }

        {
            std::lock_guard<std::mutex> lock(playback_mutex_);
            playback_chunk_index_ = index;
            playback_status_ = "Preparing chunk " + std::to_string(index + 1) + "/" +
                               std::to_string(total_chunks);
        }
        NotifyUiRefresh();

        std::string error;
        if (!pipeline_->GenerateChunkAudio(book_id, index, voice_id, &error)) {
            final_status = error.empty()
                ? "Playback failed while generating audio"
                : "Playback failed: " + error;
            break;
        }

        std::filesystem::path audio_path;
        if (!pipeline_->GetChunkAudioPath(book_id, index, voice_id, &audio_path, &error)) {
            final_status = error.empty()
                ? "Playback failed: audio path is missing"
                : "Playback failed: " + error;
            break;
        }

        {
            std::lock_guard<std::mutex> lock(playback_mutex_);
            playback_chunk_index_ = index;
            playback_status_ = "Chunk " + std::to_string(index + 1) + "/" +
                               std::to_string(total_chunks) +
                               " is ready. Playing...";
        }
        NotifyUiRefresh();

        std::string play_error;
        const bool played = audio_player_.PlayFileBlocking(
            audio_path,
            &playback_stop_requested_,
            &play_error);

        if (playback_next_requested_.exchange(false)) {
            playback_stop_requested_.store(false);
            playback_pause_requested_.store(false);
            index = std::min(index + 1, total_chunks);
            continue;
        }

        if (playback_stop_requested_.load()) {
            break;
        }

        if (!played) {
            final_status = play_error.empty()
                ? "Playback failed"
                : "Playback failed: " + play_error;
            break;
        }

        pipeline_->MarkChunkPlayed(book_id, index, voice_id, nullptr);
        ++index;
        if (single_chunk) {
            break;
        }
    }

    const bool paused = playback_pause_requested_.load();
    const bool stopped = playback_stop_requested_.load();
    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        playback_worker_running_ = false;
        playback_paused_ = paused;
        playback_chunk_index_ = std::min(index, total_chunks == 0 ? size_t{0} : total_chunks - 1);
        if (paused) {
            playback_status_ = "Paused at chunk " + std::to_string(playback_chunk_index_ + 1);
        } else if (stopped) {
            playback_status_ = "Playback stopped";
        } else if (!final_status.empty()) {
            playback_status_ = final_status;
        } else {
            playback_status_ = "Playback complete";
        }
    }

    {
        std::lock_guard<std::mutex> lock(audio_worker_mutex_);
        audio_worker_status_.clear();
        audio_worker_refresh_pending_ = true;
    }
    NotifyUiRefresh();
}

void TtsModalContent::Play() {
    bool resume_paused = false;
    size_t paused_chunk = 0;
    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        if (playback_worker_running_) {
            status_ = playback_status_.empty() ? "Playback is already running" : playback_status_;
            return;
        }
        resume_paused = playback_paused_;
        paused_chunk = playback_chunk_index_;
    }

    if (resume_paused) {
        StartPlaybackFrom(paused_chunk);
        return;
    }

    // Use the same workflow as the Run tab. This refreshes the current cursor
    // position, resolves the saved voice, reuses cached WAV files, and starts
    // playback only after all required state is ready.
    StartRunWorkflow(false, true);
}

void TtsModalContent::Pause() {
    std::lock_guard<std::mutex> lock(playback_mutex_);
    if (!playback_worker_running_) {
        status_ = playback_paused_ ? playback_status_ : "Playback is not running";
        return;
    }
    playback_pause_requested_.store(true);
    playback_stop_requested_.store(true);
    status_ = "Pausing playback...";
    playback_status_ = status_;
    NotifyUiRefresh();
}

void TtsModalContent::Stop() {
    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        if (!playback_worker_running_) {
            playback_paused_ = false;
            playback_status_ = "Playback stopped";
            status_ = "Playback stopped";
            return;
        }
        playback_pause_requested_.store(false);
        playback_next_requested_.store(false);
        playback_stop_requested_.store(true);
        status_ = "Stopping playback...";
        playback_status_ = status_;
    }
    NotifyUiRefresh();
}

void TtsModalContent::Next() {
    size_t next_chunk = 0;
    bool should_start = false;
    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        if (playback_worker_running_) {
            playback_next_requested_.store(true);
            playback_pause_requested_.store(false);
            playback_stop_requested_.store(true);
            status_ = "Skipping to next chunk...";
            playback_status_ = status_;
            NotifyUiRefresh();
            return;
        }
        if (playback_paused_) {
            next_chunk = playback_chunk_index_ + 1;
        } else if (playback_has_position_) {
            next_chunk = playback_chunk_index_;
        } else {
            next_chunk = SelectedStartChunkIndex() + 1;
        }
        playback_paused_ = false;
        should_start = true;
    }
    if (should_start) {
        StartPlaybackFrom(next_chunk, true);
    }
}
