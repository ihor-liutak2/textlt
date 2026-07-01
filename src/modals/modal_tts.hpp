#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <memory>
#include <string>
#include <vector>

#include "cloud_tts_pipeline.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

class TtsModalContent : public IModalContent {
public:
    TtsModalContent(
        const Theme* theme,
        CloudTtsPipeline* pipeline,
        std::function<void()> prepare_current_file);

    ftxui::Element Render() override;
    ~TtsModalContent() override;

    ftxui::Component GetMainComponent() override { return renderer_; }
    std::string GetTitle() override { return "Text-to-Speech"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {110, 30}; }
    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }
    std::string GetFooterText() const override { return status_; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open();
    void RefreshLibrary();

private:
    enum class Tab {
        Run = 0,
        Library = 1,
    };

    ftxui::Component MakeTextButton(std::string label, std::function<void()> on_click);
    ftxui::Component MakeTabButton(std::string label, int tab_index);
    void SelectTab(int tab_index);
    void PrepareCurrentFile();
    void SyncMetadataFieldsFromSelection();
    void SaveSelectedMetadata();
    void ShowSelectedBookInfo();
    void RebuildMetadataOptions();
    void RebuildVoiceOptions();
    ftxui::Element RenderRunTab();
    ftxui::Element RenderLibraryTab();
    ftxui::Element RenderSelectedBookSummary() const;
    ftxui::Element RenderVoiceSelector();
    ftxui::Element RenderMetadataEditor();
    ftxui::Element RenderBookInfoPanel() const;
    void SaveSelectedVoice();
    void StartGenerateAudio(size_t lookahead_count);
    void ClearSelectedAudioCache();
    std::string SelectedBookId() const;
    std::string SelectedVoiceId() const;
    size_t SelectedStartChunkIndex() const;
    std::string SelectedAudioCacheSizeText() const;
    void JoinAudioWorker();
    void ApplyAudioWorkerState();

    const Theme* theme_ = nullptr;
    CloudTtsPipeline* pipeline_ = nullptr;
    std::function<void()> prepare_current_file_;
    int selected_tab_ = static_cast<int>(Tab::Run);
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
    std::string status_ = "TTS library not loaded";
    std::thread audio_worker_;
    mutable std::mutex audio_worker_mutex_;
    bool audio_worker_running_ = false;
    std::string audio_worker_status_;
    bool audio_worker_refresh_pending_ = false;

    ftxui::Component run_tab_button_;
    ftxui::Component library_tab_button_;
    ftxui::Component tab_buttons_;

    ftxui::Component run_refresh_library_button_;
    ftxui::Component library_refresh_library_button_;
    ftxui::Component prepare_current_file_button_;
    ftxui::Component save_metadata_button_;
    ftxui::Component save_voice_button_;
    ftxui::Component generate_current_button_;
    ftxui::Component generate_next_button_;
    ftxui::Component clear_audio_cache_button_;
    ftxui::Component info_button_;
    ftxui::Component run_book_menu_;
    ftxui::Component library_book_menu_;
    ftxui::Component title_input_;
    ftxui::Component author_input_;
    ftxui::Component series_input_;
    ftxui::Component genre_input_;
    ftxui::Component series_index_input_;
    ftxui::Component language_menu_;
    ftxui::Component piper_voice_menu_;

    ftxui::Component run_tab_container_;
    ftxui::Component library_tab_container_;
    ftxui::Component tab_body_container_;
    ftxui::Component controls_;
    ftxui::Component renderer_;
};

class TtsModal {
public:
    TtsModal(
        const Theme* theme,
        CloudTtsPipeline* pipeline,
        std::function<void()> prepare_current_file);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<TtsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
