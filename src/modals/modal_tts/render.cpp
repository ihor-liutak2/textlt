/* Included by ../modal_tts.cpp. */

ftxui::Element TtsModalContent::RenderTitle() {
    return ftxui::hbox({
        run_tab_button_->Render(),
        ftxui::text(" "),
        library_tab_button_->Render(),
        ftxui::text(" "),
        voice_tab_button_->Render(),
    });
}

ftxui::Element TtsModalContent::RenderSelectedBookSummary() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (library_books_.empty() ||
        selected_library_book_ < 0 ||
        selected_library_book_ >= static_cast<int>(library_books_.size())) {
        return vbox({
            paragraph("No prepared TTS book is available yet. Prepare the current editor file first."),
        }) | color(theme.modal_text_color);
    }

    const CloudTtsPipeline::BookInfo& book = library_books_[selected_library_book_];
    const std::string voice = book.piper_voice_id.empty() ? "not selected" : book.piper_voice_id;
    const std::string language = book.language.empty() ? "unknown" : book.language;

    return vbox({
        StatusLine("Title", BookDisplayTitle(book), theme),
        StatusLine("Author", book.author.empty() ? "-" : book.author, theme),
        StatusLine("Language", language, theme),
        StatusLine("Voice", voice, theme),
        StatusLine("Chunks",
                   std::to_string(book.ready_chunks) + " ready / " +
                       std::to_string(book.prepared_chunks) + " prepared / " +
                       std::to_string(book.failed_chunks) + " failed / " +
                       std::to_string(book.total_chunks) + " total",
                   theme),
        StatusLine("Progress", FormatRatio(book.progress_ratio), theme),
        StatusLine("Current chunk", std::to_string(pipeline_ ? pipeline_->FindChunkIndexForLine(book.book_id, book.last_cursor_line) + 1 : 1), theme),
        StatusLine("Audio cache", SelectedAudioCacheSizeText(), theme),
    });
}

ftxui::Element TtsModalContent::RenderVoiceSelector() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    const std::string selected_language =
        selected_language_ >= 0 &&
                selected_language_ < static_cast<int>(language_codes_.size())
            ? language_codes_[selected_language_]
            : "unknown";

    Elements rows = {
        PanelTitle("Voice", theme),
    };

    if (library_books_.empty() ||
        selected_library_book_ < 0 ||
        selected_library_book_ >= static_cast<int>(library_books_.size())) {
        rows.push_back(paragraph("Select or prepare a book first.") |
                       color(theme.modal_text_color));
        return vbox(std::move(rows)) | border;
    }

    if (selected_language == "unknown") {
        rows.push_back(paragraph("Set the book language in the Library tab first. Then return here and select one of the installed or available voices for that language.") |
                       color(theme.modal_text_color));
        return vbox(std::move(rows)) | border;
    }

    rows.push_back(StatusLine("Language", selected_language, theme));
    rows.push_back(piper_voice_menu_->Render() |
                   frame |
                   vscroll_indicator |
                   size(HEIGHT, LESS_THAN, 10) |
                   border);

    if (selected_piper_voice_ >= 0 &&
        selected_piper_voice_ < static_cast<int>(piper_voice_installed_.size())) {
        rows.push_back(StatusLine(
            "Voice",
            piper_voice_installed_[selected_piper_voice_]
                ? "installed"
                : "not installed - use Assistant Settings / TTS to download it",
            theme));
    }

    rows.push_back(filler());
    rows.push_back(hbox({
        filler(),
        save_voice_button_->Render(),
    }));
    return vbox(std::move(rows)) | border;
}

ftxui::Element TtsModalContent::RenderRunTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Element books_panel = vbox({
        PanelTitle("Selected book", theme),
        hbox({
            run_refresh_library_button_->Render(),
        }),
        run_book_menu_->Render() |
            frame |
            vscroll_indicator |
            size(HEIGHT, LESS_THAN, 14) |
            border,
    }) | size(WIDTH, EQUAL, 34);

    Element run_panel = vbox({
        PanelTitle("Run", theme),
        paragraph("Press Run to prepare or reuse the current editor file, find the cursor chunk, generate the current chunk plus the next two, and start playback.") |
            color(theme.modal_text_color),
        RenderSelectedBookSummary() |
            border |
            size(HEIGHT, LESS_THAN, 8),
        PanelTitle("Current chunk test", theme),
        hbox({
            generate_current_button_->Render(),
            text(" "),
            clear_audio_cache_button_->Render(),
        }),
        StatusLine("Cache size", SelectedAudioCacheSizeText(), theme),
        separator() | color(theme.modal_border),
        PanelTitle("Playback", theme),
        hbox({
            play_button_->Render(),
            text(" "),
            pause_button_->Render(),
            text(" "),
            stop_button_->Render(),
            text(" "),
            next_button_->Render(),
        }),
        StatusLine("Player", TtsAudioPlayer::SelectedPlayerLabel().empty() ? TtsAudioPlayer::DependencyHelpText() : TtsAudioPlayer::SelectedPlayerLabel(), theme),
        paragraph("Play generates missing chunks on demand, plays them with a system audio player, and advances automatically. Pause stops at the current chunk; Play resumes from that chunk.") |
            dim |
            color(theme.modal_text_color),
    }) | flex;

    return hbox({
        books_panel | border,
        text(" "),
        run_panel,
    }) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

ftxui::Element TtsModalContent::RenderMetadataEditor() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (library_books_.empty() ||
        selected_library_book_ < 0 ||
        selected_library_book_ >= static_cast<int>(library_books_.size())) {
        return vbox({
            PanelTitle("Metadata", theme),
            paragraph("No prepared book selected."),
        }) | color(theme.modal_text_color);
    }

    const std::vector<std::string> series_suggestions =
        FilterSuggestions(known_series_, metadata_series_);
    const std::vector<std::string> genre_suggestions =
        FilterSuggestions(known_genres_, metadata_genre_);

    Element metadata_panel = vbox({
        PanelTitle("Metadata", theme),
        hbox({
            text(" Title: ") | bold | color(theme.modal_accent),
            title_input_->Render() | flex,
        }),
        hbox({
            text(" Author: ") | bold | color(theme.modal_accent),
            author_input_->Render() | flex,
        }),
        hbox({
            text(" Series: ") | bold | color(theme.modal_accent),
            series_input_->Render() | flex,
        }),
        SuggestionLine(series_suggestions, theme),
        hbox({
            text(" Genre: ") | bold | color(theme.modal_accent),
            genre_input_->Render() | flex,
        }),
        SuggestionLine(genre_suggestions, theme),
        hbox({
            text(" Series index: ") | bold | color(theme.modal_accent),
            series_index_input_->Render() | flex,
        }),
        save_metadata_button_->Render(),
    }) | border;

    Element language_panel = vbox({
        PanelTitle("Language", theme),
        paragraph("The selected language controls which voices are available on the Voice tab." ) |
            dim |
            color(theme.modal_text_color),
        language_menu_->Render() |
            frame |
            vscroll_indicator |
            size(HEIGHT, LESS_THAN, 8) |
            border,
    }) | border;

    return vbox({
        metadata_panel,
        language_panel,
    });
}

ftxui::Element TtsModalContent::RenderBookInfoPanel() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (library_books_.empty() ||
        selected_library_book_ < 0 ||
        selected_library_book_ >= static_cast<int>(library_books_.size())) {
        return paragraph("No prepared book selected") | color(theme.modal_text_color);
    }

    const CloudTtsPipeline::BookInfo& book = library_books_[selected_library_book_];
    return vbox({
        PanelTitle("Read-only source", theme),
        StatusLine("Book id", book.book_id, theme),
        StatusLine("Source", book.source_file_name, theme),
        StatusLine("Source path", PathToUtf8(book.source_path), theme),
        StatusLine("File size",
                   FormatBytes(static_cast<unsigned long long>(book.file_size)) + " bytes",
                   theme),
        StatusLine("Modified time", std::to_string(book.modified_time), theme),
        StatusLine("Last cursor line", std::to_string(book.last_cursor_line), theme),
        separator() | color(theme.modal_border),
        hbox({filler(), close_info_button_->Render()}),
    });
}

ftxui::Element TtsModalContent::RenderLibraryTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Element list_panel = vbox({
        PanelTitle("Books", theme),
        hbox({
            library_refresh_library_button_->Render(),
            text(" "),
            info_button_->Render(),
            text(" "),
            delete_book_button_->Render(),
        }),
        library_book_menu_->Render() |
            frame |
            vscroll_indicator |
            size(HEIGHT, LESS_THAN, 17) |
            border,
    }) | size(WIDTH, EQUAL, 34);

    return hbox({
        list_panel | border,
        text(" "),
        RenderMetadataEditor() | flex,
    }) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

ftxui::Element TtsModalContent::RenderVoiceTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return vbox({
        RenderSelectedBookSummary() | border,
        RenderVoiceSelector() | flex,
    }) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

ftxui::Element TtsModalContent::Render() {
    ApplyAudioWorkerState();

    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Element body;
    if (selected_tab_ == static_cast<int>(Tab::Run)) {
        body = RenderRunTab();
    } else if (selected_tab_ == static_cast<int>(Tab::Library)) {
        body = RenderLibraryTab();
    } else {
        body = RenderVoiceTab();
    }

    body = body |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg) |
        size(WIDTH, EQUAL, 118);

    bool audio_running = false;
    std::string audio_status;
    {
        std::lock_guard<std::mutex> lock(audio_worker_mutex_);
        audio_running = audio_worker_running_;
        audio_status = audio_worker_status_;
    }
    if (audio_running) {
        Element progress = vbox({
            hbox({
                spinner(0, audio_worker_frame_.load()) | bold,
                text("  " + (audio_status.empty() ? std::string("Preparing TTS...") : audio_status)) |
                    bold | color(theme.modal_text_color),
            }),
            text("Preparing the current file and three audio chunks. Please wait.") |
                dim | color(theme.modal_text_color),
        }) |
            border |
            bgcolor(theme.modal_background) |
            clear_under;
        return dbox({
            body,
            vbox({filler(), hbox({filler(), progress, filler()}), filler()}),
        });
    }

    if (!show_selected_book_info_) {
        return body;
    }

    Element popup_content;
    if (info_popup_mode_ == InfoPopupMode::TestText) {
        popup_content = vbox({
            PanelTitle("Chunk " + std::to_string(test_chunk_index_ + 1) + " text", theme),
            paragraph(test_text_) |
                color(theme.modal_text_color) |
                frame |
                vscroll_indicator |
                size(HEIGHT, LESS_THAN, 15),
            separator() | color(theme.modal_border),
            hbox({filler(), close_info_button_->Render()}),
        });
    } else {
        popup_content = RenderBookInfoPanel();
    }

    return dbox({
        body,
        vbox({
            filler(),
            hbox({
                filler(),
                popup_content |
                    border |
                    size(WIDTH, LESS_THAN, 94) |
                    clear_under,
                filler(),
            }),
            filler(),
        }),
    });
}
