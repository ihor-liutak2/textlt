#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <memory>
#include <string>
#include <vector>

#include "cloud_tts_pipeline.hpp"
#include "editor_config.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"
#include "tts_audio_player.hpp"
#include "ui_button.hpp"

namespace textlt {

enum class TtsHeaderButton {
    Open,
    Play,
    Pause,
    Stop,
    Next,
    Test,
};

class TtsModalContent : public IModalContent {
public:
    TtsModalContent(
        const Theme* theme,
        CloudTtsPipeline* pipeline,
        EditorConfig* editor_config,
        std::function<void(bool)> prepare_current_file,
        std::function<void()> request_ui_refresh,
        std::function<void(TtsHeaderButton)> set_header_button_active);

    ftxui::Element Render() override;
    ~TtsModalContent() override;

    ftxui::Component GetMainComponent() override { return renderer_; }
    std::string GetTitle() override { return "Text-to-Speech"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {120, 30}; }
    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }
    std::string GetFooterText() const override;

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open();
    void RefreshLibrary();
    bool IsAudioWorkerRunning() const;
    bool IsPlaybackActive() const;
    bool HasPreparedBook() const;
    std::string HeaderStatus() const;
    void Play();
    void Pause();
    void Stop();
    void Next();

private:
    enum class Tab {
        Run = 0,
        Library = 1,
        Voice = 2,
        Player = 3,
    };

    ftxui::Component MakeTextButton(std::string label,
                                    std::function<void()> on_click,
                                    ButtonRole role = ButtonRole::Secondary,
                                    ButtonVariant variant = ButtonVariant::ColoredBrackets,
                                    std::string icon = {});
    ftxui::Component MakeTabButton(std::string label, int tab_index);
    void SelectTab(int tab_index);
    void StartRunWorkflow(bool force_rebuild = false, bool play_after = true);
    void TestCurrentChunk();
    void SyncMetadataFieldsFromSelection();
    void SaveSelectedMetadata();
    void DeleteSelectedBook();
    void ShowSelectedBookInfo();
    void CloseSelectedBookInfo();
    void RebuildMetadataOptions();
    void RebuildVoiceOptions();
    void AutoSelectPreferredVoice();
    ftxui::Element RenderRunTab();
    ftxui::Element RenderLibraryTab();
    ftxui::Element RenderVoiceTab();
    ftxui::Element RenderPlayerTab();
    ftxui::Element RenderSelectedBookSummary() const;
    ftxui::Element RenderVoiceSelector();
    ftxui::Element RenderMetadataEditor();
    ftxui::Element RenderBookInfoPanel() const;
    void SaveSelectedVoice();
    void ClearSelectedAudioCache();
    void RefreshPlayerOptions();
    void SaveSelectedPlayer();
    void SaveCustomPlayerCommand();
    void TestSelectedPlayer();
    TtsAudioPlayer::PlayerSettings AudioPlayerSettings() const;
    TtsAudioPlayer::PlayerSettings SelectedPlayerSettings() const;
    std::string CurrentAudioPlayerLabel() const;
    std::string PlayerLastError() const;
    void SetPlayerLastError(std::string error);
    void StartPlaybackFrom(size_t chunk_index, bool single_chunk = false);
    void PlaybackLoop(std::string book_id,
                      std::string voice_id,
                      size_t start_chunk_index,
                      size_t total_chunks,
                      bool single_chunk,
                      TtsAudioPlayer::PlayerSettings player_settings);
    void SetPlaybackStatus(std::string status);
    void NotifyUiRefresh();
    void JoinPlaybackWorker();
    std::string SelectedBookId() const;
    std::string SelectedVoiceId() const;
    size_t SelectedStartChunkIndex() const;
    std::string SelectedAudioCacheSizeText() const;
    void JoinAudioWorker();
    void ApplyAudioWorkerState();

    const Theme* theme_ = nullptr;
    CloudTtsPipeline* pipeline_ = nullptr;
    EditorConfig* editor_config_ = nullptr;
    std::function<void(bool)> prepare_current_file_;
    std::function<void()> request_ui_refresh_;
    std::function<void(TtsHeaderButton)> set_header_button_active_;
    int selected_tab_ = static_cast<int>(Tab::Run);
    int info_layer_index_ = 0;
    std::vector<CloudTtsPipeline::BookInfo> library_books_;
    std::vector<std::string> library_book_labels_ = {"No prepared books"};
    int selected_library_book_ = 0;
    std::string metadata_title_;
    std::string metadata_author_;
    std::string metadata_series_;
    std::string metadata_genre_;
    std::string metadata_series_index_;
    std::vector<std::string> known_series_;
    std::vector<std::string> known_genres_;
    std::vector<std::string> language_codes_ = {"unknown"};
    std::vector<std::string> language_labels_ = {"Unknown"};
    int selected_language_ = 0;
    std::vector<std::string> piper_voice_ids_;
    std::vector<std::string> piper_voice_labels_ = {"No voices"};
    std::vector<bool> piper_voice_installed_;
    int selected_piper_voice_ = 0;
    bool show_selected_book_info_ = false;
    bool info_popup_pending_ = false;
    enum class InfoPopupMode { BookInfo, TestText };
    InfoPopupMode info_popup_mode_ = InfoPopupMode::BookInfo;
    std::string test_text_;
    size_t test_chunk_index_ = 0;
    std::string status_ = "TTS library not loaded";
    std::thread audio_worker_;
    mutable std::mutex audio_worker_mutex_;
    bool audio_worker_running_ = false;
    std::string audio_worker_status_;
    bool audio_worker_refresh_pending_ = false;
    bool audio_worker_play_after_ = false;
    std::string audio_worker_selected_book_id_;
    std::atomic<int> audio_worker_frame_{0};

    std::thread playback_worker_;
    mutable std::mutex playback_mutex_;
    std::atomic<bool> playback_stop_requested_{false};
    std::atomic<bool> playback_pause_requested_{false};
    std::atomic<bool> playback_next_requested_{false};
    bool playback_worker_running_ = false;
    bool playback_paused_ = false;
    bool playback_has_position_ = false;
    size_t playback_chunk_index_ = 0;
    std::string playback_status_;
    TtsAudioPlayer audio_player_;
    std::vector<TtsAudioPlayer::PlayerStatus> player_statuses_;
    std::vector<std::string> player_labels_ = {"No players detected"};
    int selected_player_ = 0;
    std::string custom_player_command_;
    std::string player_status_ = "Audio player list not loaded";
    mutable std::mutex player_mutex_;
    std::string player_last_error_;

    ftxui::Component run_tab_button_;
    ftxui::Component library_tab_button_;
    ftxui::Component voice_tab_button_;
    ftxui::Component player_tab_button_;
    ftxui::Component tab_buttons_;

    ftxui::Component run_refresh_library_button_;
    ftxui::Component library_refresh_library_button_;
    ftxui::Component save_metadata_button_;
    ftxui::Component save_voice_button_;
    ftxui::Component generate_current_button_;
    ftxui::Component play_button_;
    ftxui::Component pause_button_;
    ftxui::Component stop_button_;
    ftxui::Component next_button_;
    ftxui::Component clear_audio_cache_button_;
    ftxui::Component info_button_;
    ftxui::Component delete_book_button_;
    ftxui::Component close_info_button_;
    ftxui::Component set_player_button_;
    ftxui::Component test_player_button_;
    ftxui::Component refresh_players_button_;
    ftxui::Component save_custom_player_button_;
    ftxui::Component run_book_menu_;
    ftxui::Component library_book_menu_;
    ftxui::Component title_input_;
    ftxui::Component author_input_;
    ftxui::Component series_input_;
    ftxui::Component genre_input_;
    ftxui::Component series_index_input_;
    ftxui::Component language_menu_;
    ftxui::Component piper_voice_menu_;
    ftxui::Component player_menu_;
    ftxui::Component custom_player_input_;

    ftxui::Component run_tab_container_;
    ftxui::Component library_tab_container_;
    ftxui::Component voice_tab_container_;
    ftxui::Component player_tab_container_;
    ftxui::Component tab_body_container_;
    ftxui::Component controls_;
    ftxui::Component renderer_;
};

class TtsModal {
public:
    TtsModal(
        const Theme* theme,
        CloudTtsPipeline* pipeline,
        EditorConfig* editor_config,
        std::function<void(bool)> prepare_current_file,
        std::function<void()> request_ui_refresh,
        std::function<void(TtsHeaderButton)> set_header_button_active);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);
    bool IsAudioWorkerRunning() const;
    bool IsPlaybackActive() const;
    bool ShouldShowHeaderControls() const;
    std::string HeaderStatus() const;
    void Play();
    void Pause();
    void Stop();
    void Next();

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<TtsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
