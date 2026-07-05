/* Included by ../modal_tts.cpp. */

TtsAudioPlayer::PlayerSettings TtsModalContent::AudioPlayerSettings() const {
    TtsAudioPlayer::PlayerSettings settings;
    if (editor_config_) {
        settings.selected_player_id = editor_config_->tts_audio_player_id.empty()
            ? std::string("auto")
            : editor_config_->tts_audio_player_id;
        settings.custom_command = editor_config_->tts_audio_player_command;
    }
    return settings;
}

TtsAudioPlayer::PlayerSettings TtsModalContent::SelectedPlayerSettings() const {
    TtsAudioPlayer::PlayerSettings settings = AudioPlayerSettings();
    if (selected_player_ >= 0 && selected_player_ < static_cast<int>(player_statuses_.size())) {
        settings.selected_player_id = player_statuses_[selected_player_].command.id;
    }
    settings.custom_command = custom_player_command_;
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

void TtsModalContent::RefreshPlayerOptions() {
    const std::string previous_id =
        selected_player_ >= 0 && selected_player_ < static_cast<int>(player_statuses_.size())
            ? player_statuses_[selected_player_].command.id
            : (editor_config_ && !editor_config_->tts_audio_player_id.empty()
                   ? editor_config_->tts_audio_player_id
                   : std::string("auto"));

    custom_player_command_ = editor_config_ ? editor_config_->tts_audio_player_command : custom_player_command_;
    player_statuses_ = TtsAudioPlayer::PlayerStatuses(AudioPlayerSettings());
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
        : "Current audio player: " + current;
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
    if (selected.command.id == "custom" && custom_player_command_.empty()) {
        player_status_ = "Enter a custom command before selecting Custom command";
        status_ = player_status_;
        return;
    }
    if (!selected.available && selected.command.id != "custom") {
        player_status_ = selected.command.label + " is not available in PATH";
        status_ = player_status_;
        return;
    }

    editor_config_->tts_audio_player_id = selected.command.id;
    editor_config_->tts_audio_player_command = custom_player_command_;
    editor_config_->Persist();
    SetPlayerLastError({});
    RefreshPlayerOptions();
    player_status_ = "Audio player set to " + TtsAudioPlayer::SelectedPlayerLabel(AudioPlayerSettings());
    status_ = player_status_;
}

void TtsModalContent::SaveCustomPlayerCommand() {
    if (!editor_config_) {
        player_status_ = "Cannot save custom player command: editor config is not available";
        status_ = player_status_;
        return;
    }

    editor_config_->tts_audio_player_command = custom_player_command_;
    if (!custom_player_command_.empty() &&
        selected_player_ >= 0 && selected_player_ < static_cast<int>(player_statuses_.size()) &&
        player_statuses_[selected_player_].command.id == "custom") {
        editor_config_->tts_audio_player_id = "custom";
    } else if (custom_player_command_.empty() && editor_config_->tts_audio_player_id == "custom") {
        editor_config_->tts_audio_player_id = "auto";
    }
    editor_config_->Persist();
    SetPlayerLastError({});
    RefreshPlayerOptions();
    player_status_ = custom_player_command_.empty()
        ? "Custom player command cleared"
        : "Custom player command saved";
    status_ = player_status_;
}

void TtsModalContent::TestSelectedPlayer() {
    if (selected_player_ < 0 || selected_player_ >= static_cast<int>(player_statuses_.size())) {
        player_status_ = "No audio player selected";
        status_ = player_status_;
        return;
    }

    const TtsAudioPlayer::PlayerStatus selected = player_statuses_[selected_player_];
    if (selected.command.id == "custom" && custom_player_command_.empty()) {
        player_status_ = "Enter a custom command before testing Custom command";
        status_ = player_status_;
        SetPlayerLastError(player_status_);
        return;
    }
    if (!selected.available && selected.command.id != "custom") {
        player_status_ = selected.command.label + " is not available in PATH";
        status_ = player_status_;
        SetPlayerLastError(player_status_);
        return;
    }

    SaveSelectedPlayer();
    if (!HasPreparedBook()) {
        player_status_ = "Player is available. Select or prepare a book to test real playback.";
        status_ = player_status_;
        return;
    }
    player_status_ = "Testing selected player with the current chunk...";
    status_ = player_status_;
    StartPlaybackFrom(SelectedStartChunkIndex(), true);
}
