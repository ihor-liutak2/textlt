/* Included by ../modal_tts.cpp. */

namespace {

constexpr const char* kTtsPlayerTestText =
    "This is the TextLT audio player test. If you can hear this sentence, "
    "your selected audio player is working correctly.";

std::filesystem::path TtsPlayerTestAudioPath() {
    const std::filesystem::path data_directory = PiperManager::UserDataDirectory();
    if (data_directory.empty()) {
        return {};
    }
    return data_directory / "tts" / "player_test.wav";
}

} // namespace

TtsAudioPlayer::PlayerSettings TtsModalContent::AudioPlayerSettings() const {
    TtsAudioPlayer::PlayerSettings settings;
    if (editor_config_) {
        settings.selected_player_id = editor_config_->tts_audio_player_id.empty()
            ? std::string("auto")
            : editor_config_->tts_audio_player_id;
        if (settings.selected_player_id == "custom") {
            settings.selected_player_id = "auto";
        }
    }
    return settings;
}

TtsAudioPlayer::PlayerSettings TtsModalContent::SelectedPlayerSettings() const {
    TtsAudioPlayer::PlayerSettings settings = AudioPlayerSettings();
    if (selected_player_ >= 0 && selected_player_ < static_cast<int>(player_statuses_.size())) {
        settings.selected_player_id = player_statuses_[selected_player_].command.id;
    }
    return settings;
}

std::string TtsModalContent::CurrentAudioPlayerLabel() const {
    const std::string label = TtsAudioPlayer::SelectedPlayerLabel(AudioPlayerSettings());
    return label.empty() ? TtsAudioPlayer::DependencyHelpText() : label;
}

std::string TtsModalContent::PlayerLastError() const {
    std::lock_guard<std::mutex> lock(player_mutex_);
    return player_last_error_;
}

void TtsModalContent::SetPlayerLastError(std::string error) {
    std::lock_guard<std::mutex> lock(player_mutex_);
    player_last_error_ = std::move(error);
}

void TtsModalContent::RefreshPlayerVoiceOptions() {
    std::string previous_id = editor_config_ ? editor_config_->tts_player_voice_id : std::string{};
    if (previous_id.empty() &&
        selected_player_voice_ >= 0 &&
        selected_player_voice_ < static_cast<int>(player_voice_ids_.size())) {
        previous_id = player_voice_ids_[selected_player_voice_];
    }

    player_voice_ids_.clear();
    player_voice_labels_.clear();
    selected_player_voice_ = 0;

    if (!pipeline_) {
        player_voice_labels_.push_back("TTS pipeline is not available");
        return;
    }

    auto append_language = [this](const std::string& code, const std::string& label_prefix) {
        const std::vector<CloudTtsPipeline::PiperVoiceOption> voices =
            pipeline_->ListPiperVoiceOptions(code);
        for (const CloudTtsPipeline::PiperVoiceOption& voice : voices) {
            if (!voice.installed || voice.id.empty()) {
                continue;
            }
            if (std::find(player_voice_ids_.begin(), player_voice_ids_.end(), voice.id) !=
                player_voice_ids_.end()) {
                continue;
            }
            player_voice_ids_.push_back(voice.id);
            player_voice_labels_.push_back(label_prefix + " | " + voice.label);
        }
    };

    append_language("en_US", "US English");
    append_language("en_GB", "British English");

    if (player_voice_ids_.empty()) {
        player_voice_labels_.push_back("No installed US/GB English voices");
        return;
    }

    if (!previous_id.empty()) {
        for (size_t index = 0; index < player_voice_ids_.size(); ++index) {
            if (player_voice_ids_[index] == previous_id) {
                selected_player_voice_ = static_cast<int>(index);
                return;
            }
        }
    }

    auto find_quality = [this](const std::string& quality) {
        for (size_t index = 0; index < player_voice_ids_.size(); ++index) {
            const std::string id = LowerAscii(player_voice_ids_[index]);
            const std::string label = LowerAscii(player_voice_labels_[index]);
            if (id.find(quality) != std::string::npos ||
                label.find(quality) != std::string::npos) {
                return static_cast<int>(index);
            }
        }
        return -1;
    };

    int preferred = find_quality("medium");
    if (preferred < 0) {
        preferred = find_quality("high");
    }
    selected_player_voice_ = preferred < 0 ? 0 : preferred;
}

void TtsModalContent::SaveSelectedPlayerVoice() {
    if (!editor_config_ ||
        selected_player_voice_ < 0 ||
        selected_player_voice_ >= static_cast<int>(player_voice_ids_.size())) {
        return;
    }
    editor_config_->tts_player_voice_id = player_voice_ids_[selected_player_voice_];
    editor_config_->Persist();
}

std::string TtsModalContent::SelectedPlayerVoiceId() const {
    if (selected_player_voice_ >= 0 &&
        selected_player_voice_ < static_cast<int>(player_voice_ids_.size())) {
        return player_voice_ids_[selected_player_voice_];
    }
    return editor_config_ ? editor_config_->tts_player_voice_id : std::string{};
}

std::string TtsModalContent::CurrentPlayerVoiceLabel() const {
    if (selected_player_voice_ >= 0 &&
        selected_player_voice_ < static_cast<int>(player_voice_labels_.size()) &&
        selected_player_voice_ < static_cast<int>(player_voice_ids_.size())) {
        return player_voice_labels_[selected_player_voice_];
    }
    return {};
}

void TtsModalContent::RefreshPlayerOptions() {
    const std::string previous_id =
        selected_player_ >= 0 && selected_player_ < static_cast<int>(player_statuses_.size())
            ? player_statuses_[selected_player_].command.id
            : (editor_config_ && !editor_config_->tts_audio_player_id.empty()
                   ? editor_config_->tts_audio_player_id
                   : std::string("auto"));

    player_statuses_ = TtsAudioPlayer::PlayerStatuses(AudioPlayerSettings());
    player_statuses_.erase(
        std::remove_if(
            player_statuses_.begin(),
            player_statuses_.end(),
            [](const TtsAudioPlayer::PlayerStatus& status) {
                return status.command.id == "custom";
            }),
        player_statuses_.end());
    player_labels_.clear();

    int current_index = -1;
    int previous_index = -1;
    int first_available_index = -1;
    for (size_t index = 0; index < player_statuses_.size(); ++index) {
        const TtsAudioPlayer::PlayerStatus& status = player_statuses_[index];
        std::string label = status.current ? "✓ " : "  ";
        label += status.command.label;
        label += " | ";
        label += status.status_text;
        player_labels_.push_back(std::move(label));

        if (status.current) {
            current_index = static_cast<int>(index);
        }
        if (status.command.id == previous_id) {
            previous_index = static_cast<int>(index);
        }
        if (first_available_index < 0 && status.available) {
            first_available_index = static_cast<int>(index);
        }
    }

    if (player_labels_.empty()) {
        player_labels_.push_back("No players detected");
        selected_player_ = 0;
    } else if (previous_index >= 0) {
        selected_player_ = previous_index;
    } else if (current_index >= 0) {
        selected_player_ = current_index;
    } else if (first_available_index >= 0) {
        selected_player_ = first_available_index;
    } else {
        selected_player_ = 0;
    }

    const std::string current = TtsAudioPlayer::SelectedPlayerLabel(AudioPlayerSettings());
    player_status_ = current.empty()
        ? TtsAudioPlayer::DependencyHelpText()
        : "Ready. Select a player or press Test to play the built-in sample text.";
    NotifyUiRefresh();
}

void TtsModalContent::SaveSelectedPlayer() {
    if (!editor_config_) {
        player_status_ = "Cannot save player: editor config is not available";
        status_ = player_status_;
        return;
    }
    if (selected_player_ < 0 || selected_player_ >= static_cast<int>(player_statuses_.size())) {
        player_status_ = "No audio player selected";
        status_ = player_status_;
        return;
    }

    TtsAudioPlayer::PlayerStatus selected = player_statuses_[selected_player_];
    if (!selected.available) {
        player_status_ = selected.command.label + " is not available in PATH";
        status_ = player_status_;
        return;
    }

    editor_config_->tts_audio_player_id = selected.command.id;
    editor_config_->Persist();
    SetPlayerLastError({});
    RefreshPlayerOptions();
    player_status_ = "Audio player set to " + TtsAudioPlayer::SelectedPlayerLabel(AudioPlayerSettings());
    status_ = player_status_;
}

void TtsModalContent::TestSelectedPlayer() {
    if (selected_player_ < 0 || selected_player_ >= static_cast<int>(player_statuses_.size())) {
        player_status_ = "No audio player selected";
        status_ = player_status_;
        return;
    }

    const TtsAudioPlayer::PlayerStatus selected = player_statuses_[selected_player_];
    if (!selected.available) {
        player_status_ = selected.command.label + " is not available in PATH";
        status_ = player_status_;
        SetPlayerLastError(player_status_);
        return;
    }

    const std::string voice_id = SelectedPlayerVoiceId();
    if (voice_id.empty()) {
        player_status_ = "No installed US/GB English Piper voice selected";
        status_ = player_status_;
        SetPlayerLastError(player_status_);
        return;
    }
    if (!PiperManager::RuntimeInstalled()) {
        player_status_ = "Piper runtime is not installed. Use TTS Settings / TTS / Install Piper.";
        status_ = player_status_;
        SetPlayerLastError(player_status_);
        return;
    }

    Json voice;
    if (!PiperManager::FindVoiceById(voice_id, &voice)) {
        player_status_ = "Selected player test voice is not in the registry";
        status_ = player_status_;
        SetPlayerLastError(player_status_);
        return;
    }
    if (!PiperManager::VoiceInstalled(voice)) {
        player_status_ = "Selected player test voice files are missing. Use TTS Settings / TTS to download it.";
        status_ = player_status_;
        SetPlayerLastError(player_status_);
        return;
    }

    SaveSelectedPlayer();
    SaveSelectedPlayerVoice();
    const TtsAudioPlayer::PlayerSettings player_settings = AudioPlayerSettings();
    const std::filesystem::path audio_path = TtsPlayerTestAudioPath();
    if (audio_path.empty()) {
        player_status_ = "Cannot create TTS player test audio path";
        status_ = player_status_;
        SetPlayerLastError(player_status_);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        if (playback_worker_running_) {
            player_status_ = playback_status_.empty() ? "Playback is already running" : playback_status_;
            status_ = player_status_;
            return;
        }
    }

    JoinPlaybackWorker();
    playback_stop_requested_.store(false);
    playback_pause_requested_.store(false);
    playback_next_requested_.store(false);

    test_text_title_ = "Audio player test text";
    test_text_ = kTtsPlayerTestText;
    test_chunk_index_ = 0;
    info_popup_mode_ = InfoPopupMode::TestText;
    info_popup_pending_ = true;

    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        playback_worker_running_ = true;
        playback_paused_ = false;
        playback_has_position_ = false;
        playback_status_ = "Generating built-in player test audio with " + CurrentPlayerVoiceLabel();
    }
    player_status_ = "Generating built-in player test audio with " + CurrentPlayerVoiceLabel() + "...";
    status_ = player_status_;

    playback_worker_ = std::thread(
        [this, voice, audio_path, player_settings] {
            std::string error;
            std::error_code directory_error;
            std::filesystem::create_directories(audio_path.parent_path(), directory_error);
            bool generated = false;
            if (directory_error) {
                error = "Cannot create player test directory";
            } else {
                generated = PiperManager::RunToFile(
                    voice,
                    std::string(kTtsPlayerTestText),
                    audio_path,
                    &error);
            }

            bool played = false;
            if (generated) {
                SetPlaybackStatus("Playing built-in player test audio");
                std::string play_error;
                played = audio_player_.PlayFileBlocking(
                    audio_path,
                    &playback_stop_requested_,
                    &play_error,
                    player_settings);
                if (!played && error.empty()) {
                    error = play_error.empty() ? "Playback failed" : play_error;
                }
            }

            const bool stopped = playback_stop_requested_.load();
            std::string final_status;
            if (stopped) {
                final_status = "Player test stopped";
            } else if (generated && played) {
                final_status = "Player test played successfully";
                SetPlayerLastError({});
            } else if (generated) {
                final_status = error.empty()
                    ? "Player test audio generated, playback failed"
                    : "Player test audio generated, playback failed: " + error;
                SetPlayerLastError(error.empty() ? "Playback failed" : error);
            } else {
                final_status = error.empty()
                    ? "Player test failed"
                    : "Player test failed: " + error;
                SetPlayerLastError(error.empty() ? "Player test failed" : error);
            }

            {
                std::lock_guard<std::mutex> lock(playback_mutex_);
                playback_worker_running_ = false;
                playback_paused_ = false;
                playback_status_ = final_status;
            }
            player_status_ = final_status;
            status_ = final_status;
            NotifyUiRefresh();
        });
    NotifyUiRefresh();
}
