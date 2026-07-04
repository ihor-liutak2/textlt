#include "modal_tts.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <filesystem>
#include <future>
#include <iomanip>
#include <sstream>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

ftxui::Element StatusLine(const std::string& label,
                          const std::string& value,
                          const Theme& theme) {
    using namespace ftxui;
    return hbox({
        text(" " + label + ": ") | bold | color(theme.modal_accent),
        paragraph(value.empty() ? "-" : value) | color(theme.modal_text_color) | flex,
    });
}

ftxui::Element PanelTitle(const std::string& label, const Theme& theme) {
    return ftxui::text(" " + label) |
           ftxui::bold |
           ftxui::color(theme.modal_text_color);
}

std::string BracketLabel(const std::string& label) {
    return "[ " + label + " ]";
}

std::string FormatBytes(unsigned long long value) {
    std::string text = std::to_string(value);
    std::string formatted;
    int group_count = 0;
    for (auto iter = text.rbegin(); iter != text.rend(); ++iter) {
        if (group_count == 3) {
            formatted.push_back(' ');
            group_count = 0;
        }
        formatted.push_back(*iter);
        ++group_count;
    }
    std::reverse(formatted.begin(), formatted.end());
    return formatted;
}


std::string FormatHumanBytes(unsigned long long value) {
    const double kib = 1024.0;
    const double mib = kib * 1024.0;
    if (value >= static_cast<unsigned long long>(mib)) {
        std::ostringstream output;
        output << std::fixed << std::setprecision(1) << (static_cast<double>(value) / mib) << " MB";
        return output.str();
    }
    if (value >= static_cast<unsigned long long>(kib)) {
        std::ostringstream output;
        output << std::fixed << std::setprecision(1) << (static_cast<double>(value) / kib) << " KB";
        return output.str();
    }
    return FormatBytes(value) + " bytes";
}

std::string FormatRatio(double ratio) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(0)
           << (std::max)(0.0, (std::min)(1.0, ratio)) * 100.0 << "%";
    return output.str();
}

std::string PathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    return path.u8string();
#else
    return path.string();
#endif
}

std::string BookDisplayTitle(const CloudTtsPipeline::BookInfo& book) {
    if (!book.title.empty()) {
        return book.title;
    }
    if (!book.source_file_name.empty()) {
        return book.source_file_name;
    }
    return book.book_id;
}

std::string BookListLabel(const CloudTtsPipeline::BookInfo& book) {
    std::string label = BookDisplayTitle(book);
    if (!book.author.empty()) {
        label += " | " + book.author;
    }
    label += " | " + FormatRatio(book.progress_ratio);
    return label;
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::vector<std::string> FilterSuggestions(const std::vector<std::string>& values,
                                           const std::string& input) {
    std::vector<std::string> suggestions;
    const std::string lowered_input = LowerAscii(input);
    for (const std::string& value : values) {
        if (value.empty() || value == input) {
            continue;
        }
        if (lowered_input.empty() ||
            LowerAscii(value).find(lowered_input) != std::string::npos) {
            suggestions.push_back(value);
        }
        if (suggestions.size() >= 4) {
            break;
        }
    }
    return suggestions;
}

ftxui::Element SuggestionLine(const std::vector<std::string>& suggestions,
                              const Theme& theme) {
    if (suggestions.empty()) {
        return ftxui::text("");
    }

    std::string text = " Suggestions: ";
    for (size_t index = 0; index < suggestions.size(); ++index) {
        if (index > 0) {
            text += ", ";
        }
        text += suggestions[index];
    }
    return ftxui::paragraph(text) |
           ftxui::dim |
           ftxui::color(theme.modal_text_color);
}

} // namespace

TtsModalContent::TtsModalContent(
    const Theme* theme,
    CloudTtsPipeline* pipeline,
    std::function<void(bool)> prepare_current_file,
    std::function<void()> request_ui_refresh,
    std::function<void(TtsHeaderButton)> set_header_button_active)
    : theme_(theme),
      pipeline_(pipeline),
      prepare_current_file_(std::move(prepare_current_file)),
      request_ui_refresh_(std::move(request_ui_refresh)),
      set_header_button_active_(std::move(set_header_button_active)) {
    run_tab_button_ = MakeTabButton("Run", static_cast<int>(Tab::Run));
    library_tab_button_ = MakeTabButton("Library", static_cast<int>(Tab::Library));
    voice_tab_button_ = MakeTabButton("Voice", static_cast<int>(Tab::Voice));
    tab_buttons_ = ftxui::Container::Horizontal({
        run_tab_button_,
        library_tab_button_,
        voice_tab_button_,
    });

    run_refresh_library_button_ =
        MakeTextButton("Refresh", [this] { StartRunWorkflow(true, false); });
    library_refresh_library_button_ =
        MakeTextButton("Refresh", [this] { StartRunWorkflow(true, false); });
    save_metadata_button_ =
        MakeTextButton("Save metadata", [this] { SaveSelectedMetadata(); });
    save_voice_button_ =
        MakeTextButton("Save voice", [this] { SaveSelectedVoice(); });
    generate_current_button_ =
        MakeTextButton("Test", [this] { TestCurrentChunk(); });
    play_button_ =
        MakeTextButton("Play", [this] { Play(); });
    pause_button_ =
        MakeTextButton("Pause", [this] { Pause(); });
    stop_button_ =
        MakeTextButton("Stop", [this] { Stop(); });
    next_button_ =
        MakeTextButton("Next", [this] { Next(); });
    clear_audio_cache_button_ =
        MakeTextButton("Clear audio cache", [this] { ClearSelectedAudioCache(); });
    info_button_ =
        MakeTextButton("Info", [this] { ShowSelectedBookInfo(); });
    delete_book_button_ =
        MakeTextButton("Delete", [this] { DeleteSelectedBook(); });
    close_info_button_ =
        MakeTextButton("Close", [this] { CloseSelectedBookInfo(); });

    auto make_book_menu = [this] {
        ftxui::MenuOption menu_option = ftxui::MenuOption::Vertical();
        menu_option.on_change = [this] { SyncMetadataFieldsFromSelection(); };
        menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            ftxui::Element row = ftxui::text(" " + state.label + " ");
            if (state.focused || state.active) {
                return row |
                    ftxui::bgcolor(theme.modal_selected_item_bg) |
                    ftxui::color(theme.modal_selected_item_fg) |
                    ftxui::bold;
            }
            return row | ftxui::color(theme.modal_text_color);
        };
        return ftxui::Menu(&library_book_labels_, &selected_library_book_, menu_option);
    };
    run_book_menu_ = make_book_menu();
    library_book_menu_ = make_book_menu();

    ftxui::InputOption input_option;
    input_option.multiline = false;
    input_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    title_input_ = ftxui::Input(&metadata_title_, "title", input_option);
    author_input_ = ftxui::Input(&metadata_author_, "author", input_option);
    series_input_ = ftxui::Input(&metadata_series_, "series", input_option);
    genre_input_ = ftxui::Input(&metadata_genre_, "genre", input_option);
    series_index_input_ =
        ftxui::Input(&metadata_series_index_, "series index", input_option);

    ftxui::MenuOption language_option = ftxui::MenuOption::Vertical();
    language_option.on_change = [this] {
        RebuildVoiceOptions();
        SaveSelectedMetadata();
    };
    language_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row =
            ftxui::text(std::string(state.active ? "[x] " : "[ ] ") + state.label);
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg) |
                ftxui::bold;
        }
        return row | ftxui::color(theme.modal_text_color);
    };
    language_menu_ = ftxui::Menu(&language_labels_, &selected_language_, language_option);

    ftxui::MenuOption voice_option = ftxui::MenuOption::Vertical();
    voice_option.entries_option.transform = language_option.entries_option.transform;
    piper_voice_menu_ =
        ftxui::Menu(&piper_voice_labels_, &selected_piper_voice_, voice_option);

    run_tab_container_ = ftxui::Container::Vertical({
        run_refresh_library_button_,
        run_book_menu_,
        ftxui::Container::Horizontal({
            generate_current_button_,
            clear_audio_cache_button_,
        }),
        ftxui::Container::Horizontal({
            play_button_,
            pause_button_,
            stop_button_,
            next_button_,
        }),
    });

    library_tab_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            library_refresh_library_button_,
            info_button_,
            delete_book_button_,
        }),
        library_book_menu_,
        title_input_,
        author_input_,
        series_input_,
        genre_input_,
        series_index_input_,
        language_menu_,
        save_metadata_button_,
        close_info_button_,
    });

    voice_tab_container_ = ftxui::Container::Vertical({
        piper_voice_menu_,
        save_voice_button_,
    });

    tab_body_container_ = ftxui::Container::Tab({
        run_tab_container_,
        library_tab_container_,
        voice_tab_container_,
    }, &selected_tab_);

    auto primary_controls = ftxui::Container::Vertical({
        tab_buttons_,
        tab_body_container_,
    });
    auto info_controls = ftxui::CatchEvent(close_info_button_, [this](ftxui::Event event) {
        if (event == ftxui::Event::Escape) {
            CloseSelectedBookInfo();
            return true;
        }
        return false;
    });
    controls_ = ftxui::Container::Tab(
        {primary_controls, info_controls}, &info_layer_index_);
    controls_ = ftxui::CatchEvent(controls_, [this](ftxui::Event event) {
        if (event == ftxui::Event::Custom && info_popup_pending_) {
            info_popup_pending_ = false;
            show_selected_book_info_ = true;
            info_layer_index_ = 1;
            close_info_button_->TakeFocus();
            return true;
        }
        return false;
    });
    renderer_ = ftxui::Renderer(controls_, [this] { return Render(); });
    RefreshLibrary();
}

TtsModalContent::~TtsModalContent() {
    Stop();
    JoinPlaybackWorker();
    JoinAudioWorker();
}

ftxui::Component TtsModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = std::move(on_click);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element button = ftxui::text(BracketLabel(state.label));
        if (state.focused || state.active) {
            return button |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return button | ftxui::color(theme.modal_accent);
    };
    return ftxui::Button(option);
}

ftxui::Component TtsModalContent::MakeTabButton(std::string label, int tab_index) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = [this, tab_index] { SelectTab(tab_index); };
    option.transform = [this, tab_index](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element tab = ftxui::text(BracketLabel(state.label));
        if (selected_tab_ == tab_index || state.focused || state.active) {
            return tab |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg) |
                ftxui::bold;
        }
        return tab | ftxui::color(theme.modal_text_color) | ftxui::dim;
    };
    return ftxui::Button(option);
}

void TtsModalContent::SelectTab(int tab_index) {
    selected_tab_ = tab_index;
    if (selected_tab_ == static_cast<int>(Tab::Run)) {
        if (play_button_) {
            play_button_->TakeFocus();
        }
    } else if (selected_tab_ == static_cast<int>(Tab::Library) && library_book_menu_) {
        library_book_menu_->TakeFocus();
    } else if (selected_tab_ == static_cast<int>(Tab::Voice) && piper_voice_menu_) {
        piper_voice_menu_->TakeFocus();
    }
}

void TtsModalContent::Open() {
    selected_tab_ = static_cast<int>(Tab::Library);
    show_selected_book_info_ = false;
    info_layer_index_ = 0;
    RefreshLibrary();
    if (library_book_menu_) {
        library_book_menu_->TakeFocus();
    }
}


#include "modal_tts/generation.cpp"
#include "modal_tts/playback.cpp"
#include "modal_tts/library_status.cpp"
#include "modal_tts/render.cpp"

TtsModal::TtsModal(
    const Theme* theme,
    CloudTtsPipeline* pipeline,
    std::function<void(bool)> prepare_current_file,
    std::function<void()> request_ui_refresh,
    std::function<void(TtsHeaderButton)> set_header_button_active)
    : theme_(theme) {
    content_ = std::make_shared<TtsModalContent>(
        theme_,
        pipeline,
        std::move(prepare_current_file),
        std::move(request_ui_refresh),
        std::move(set_header_button_active));
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterText("Escape closes.");
    modal_->SetFooterButtons({{"Close", [this] { Close(); }}});
    modal_->SetBodyFrameScrolling(true);
}

ftxui::Component TtsModal::View() const {
    return modal_;
}

void TtsModal::Open() {
    open_ = true;
    content_->SetTheme(theme_);
    content_->Open();
    modal_->SetTheme(theme_);
    content_->GetMainComponent()->TakeFocus();
}

void TtsModal::Close() {
    open_ = false;
}

bool TtsModal::IsOpen() const {
    return open_;
}

bool TtsModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}

bool TtsModal::IsAudioWorkerRunning() const {
    return content_ && content_->IsAudioWorkerRunning();
}

bool TtsModal::IsPlaybackActive() const {
    return content_ && content_->IsPlaybackActive();
}

bool TtsModal::ShouldShowHeaderControls() const {
    return content_ &&
           (content_->IsAudioWorkerRunning() || content_->IsPlaybackActive() || content_->HasPreparedBook());
}

std::string TtsModal::HeaderStatus() const {
    return content_ ? content_->HeaderStatus() : std::string();
}

void TtsModal::Play() {
    if (content_) {
        content_->Play();
    }
}

void TtsModal::Pause() {
    if (content_) {
        content_->Pause();
    }
}

void TtsModal::Stop() {
    if (content_) {
        content_->Stop();
    }
}

void TtsModal::Next() {
    if (content_) {
        content_->Next();
    }
}

} // namespace textlt
