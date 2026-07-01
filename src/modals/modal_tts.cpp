#include "modal_tts.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
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
    std::function<void()> prepare_current_file)
    : theme_(theme),
      pipeline_(pipeline),
      prepare_current_file_(std::move(prepare_current_file)) {
    run_tab_button_ = MakeTabButton("Run", static_cast<int>(Tab::Run));
    library_tab_button_ = MakeTabButton("Library", static_cast<int>(Tab::Library));
    tab_buttons_ = ftxui::Container::Horizontal({
        run_tab_button_,
        library_tab_button_,
    });

    run_refresh_library_button_ =
        MakeTextButton("Refresh", [this] { RefreshLibrary(); });
    library_refresh_library_button_ =
        MakeTextButton("Refresh", [this] { RefreshLibrary(); });
    prepare_current_file_button_ =
        MakeTextButton("Prepare current file", [this] { PrepareCurrentFile(); });
    save_metadata_button_ =
        MakeTextButton("Save metadata", [this] { SaveSelectedMetadata(); });
    save_voice_button_ =
        MakeTextButton("Save voice", [this] { SaveSelectedVoice(); });
    generate_current_button_ =
        MakeTextButton("Generate current", [this] { StartGenerateAudio(1); });
    generate_next_button_ =
        MakeTextButton("Generate next 3", [this] { StartGenerateAudio(4); });
    clear_audio_cache_button_ =
        MakeTextButton("Clear audio cache", [this] { ClearSelectedAudioCache(); });
    info_button_ =
        MakeTextButton("Info", [this] { ShowSelectedBookInfo(); });

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
    title_input_ = ftxui::Input(&metadata_title_, "title", input_option);
    author_input_ = ftxui::Input(&metadata_author_, "author", input_option);
    series_input_ = ftxui::Input(&metadata_series_, "series", input_option);
    genre_input_ = ftxui::Input(&metadata_genre_, "genre", input_option);
    series_index_input_ =
        ftxui::Input(&metadata_series_index_, "series index", input_option);

    ftxui::MenuOption language_option = ftxui::MenuOption::Vertical();
    language_option.on_change = [this] { RebuildVoiceOptions(); };
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
        ftxui::Container::Horizontal({
            run_refresh_library_button_,
            prepare_current_file_button_,
        }),
        run_book_menu_,
        piper_voice_menu_,
        save_voice_button_,
        ftxui::Container::Horizontal({
            generate_current_button_,
            generate_next_button_,
            clear_audio_cache_button_,
        }),
    });

    library_tab_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            library_refresh_library_button_,
            info_button_,
        }),
        library_book_menu_,
        title_input_,
        author_input_,
        series_input_,
        genre_input_,
        series_index_input_,
        language_menu_,
        save_metadata_button_,
    });

    tab_body_container_ = ftxui::Container::Tab({
        run_tab_container_,
        library_tab_container_,
    }, &selected_tab_);

    controls_ = ftxui::Container::Vertical({
        tab_buttons_,
        tab_body_container_,
    });
    renderer_ = ftxui::Renderer(controls_, [this] { return Render(); });
    RefreshLibrary();
}

TtsModalContent::~TtsModalContent() {
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
    if (selected_tab_ == static_cast<int>(Tab::Run) && prepare_current_file_button_) {
        prepare_current_file_button_->TakeFocus();
    } else if (selected_tab_ == static_cast<int>(Tab::Library) && library_book_menu_) {
        library_book_menu_->TakeFocus();
    }
}

void TtsModalContent::Open() {
    selected_tab_ = static_cast<int>(Tab::Run);
    RefreshLibrary();
    if (prepare_current_file_button_) {
        prepare_current_file_button_->TakeFocus();
    }
}

void TtsModalContent::PrepareCurrentFile() {
    if (!prepare_current_file_) {
        status_ = "Current document is not available to TTS UI";
        return;
    }
    prepare_current_file_();
    RefreshLibrary();
    status_ = "Queued current file preparation";
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
    }
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
        return;
    }

    show_selected_book_info_ = false;
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
    selected_piper_voice_ = 0;
    if (!book.piper_voice_id.empty()) {
        for (size_t index = 0; index < piper_voice_ids_.size(); ++index) {
            if (piper_voice_ids_[index] == book.piper_voice_id) {
                selected_piper_voice_ = static_cast<int>(index);
                break;
            }
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
    std::string worker_status;
    {
        std::lock_guard<std::mutex> lock(audio_worker_mutex_);
        if (!audio_worker_refresh_pending_) {
            return;
        }
        audio_worker_refresh_pending_ = false;
        should_refresh = true;
        worker_status = audio_worker_status_;
    }

    if (should_refresh) {
        RefreshLibrary();
        if (!worker_status.empty()) {
            status_ = worker_status;
        }
    }
}

void TtsModalContent::StartGenerateAudio(size_t lookahead_count) {
    if (!pipeline_) {
        status_ = "TTS pipeline is not available";
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

    {
        std::lock_guard<std::mutex> lock(audio_worker_mutex_);
        if (audio_worker_running_) {
            status_ = "Audio generation is already running";
            return;
        }
    }
    JoinAudioWorker();

    const size_t start_chunk = SelectedStartChunkIndex();
    {
        std::lock_guard<std::mutex> lock(audio_worker_mutex_);
        audio_worker_running_ = true;
        audio_worker_status_ = "Generating audio...";
        status_ = "Generating audio...";
    }

    audio_worker_ = std::thread([this, book_id, voice_id, start_chunk, lookahead_count] {
        std::string error;
        CloudTtsPipeline::AudioGenerationResult result =
            pipeline_->EnsureAudioLookahead(
                book_id,
                start_chunk,
                voice_id,
                lookahead_count,
                &error);

        std::ostringstream message;
        message << "Audio: " << result.generated_chunks << " generated, "
                << result.already_ready_chunks << " already ready";
        if (result.failed_chunks > 0) {
            message << ", " << result.failed_chunks << " failed";
            if (!error.empty()) {
                message << " - " << error;
            }
        }
        const std::string final_status = message.str();
        {
            std::lock_guard<std::mutex> lock(audio_worker_mutex_);
            audio_worker_running_ = false;
            audio_worker_status_ = final_status;
            audio_worker_refresh_pending_ = true;
        }
    });
}

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
    show_selected_book_info_ = !show_selected_book_info_;
    status_ = show_selected_book_info_ ? "Book info shown" : "Book info hidden";
}

ftxui::Element TtsModalContent::RenderTitle() {
    return ftxui::hbox({
        run_tab_button_->Render(),
        ftxui::text(" "),
        library_tab_button_->Render(),
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

    rows.push_back(save_voice_button_->Render());
    return vbox(std::move(rows)) | border;
}

ftxui::Element TtsModalContent::RenderRunTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Element books_panel = vbox({
        PanelTitle("Selected book", theme),
        hbox({
            run_refresh_library_button_->Render(),
            text(" "),
            prepare_current_file_button_->Render(),
        }),
        run_book_menu_->Render() |
            frame |
            vscroll_indicator |
            size(HEIGHT, LESS_THAN, 14) |
            border,
    }) | size(WIDTH, EQUAL, 34);

    Element run_panel = vbox({
        PanelTitle("Run", theme),
        paragraph("Prepare the current editor file as a TTS book. Select the book here, choose a voice for its saved language, then audio generation/playback can use that saved choice.") |
            color(theme.modal_text_color),
        RenderSelectedBookSummary() |
            border |
            size(HEIGHT, LESS_THAN, 8),
        RenderVoiceSelector() | flex,
        separator() | color(theme.modal_border),
        PanelTitle("Audio generation", theme),
        hbox({
            generate_current_button_->Render(),
            text(" "),
            generate_next_button_->Render(),
            text(" "),
            clear_audio_cache_button_->Render(),
        }),
        StatusLine("Cache size", SelectedAudioCacheSizeText(), theme),
        paragraph("Generate current creates audio for the current chunk. Generate next 3 keeps a small cache ready for future playback. Voice download/runtime install stays in Assistant Settings / TTS.") |
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

    Element metadata_column = vbox({
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
    }) | size(WIDTH, EQUAL, 38);

    Element language_column = vbox({
        PanelTitle("Language", theme),
        paragraph("The selected language controls which voices are available on the Run tab." ) |
            dim |
            color(theme.modal_text_color),
        language_menu_->Render() |
            frame |
            vscroll_indicator |
            size(HEIGHT, LESS_THAN, 15) |
            border,
    }) | flex;

    Elements rows = {
        hbox({
            metadata_column | border,
            text(" "),
            language_column | border | flex,
        }) | flex,
    };

    if (show_selected_book_info_) {
        rows.push_back(RenderBookInfoPanel() |
                       border |
                       size(HEIGHT, LESS_THAN, 7));
    }

    return vbox(std::move(rows));
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

ftxui::Element TtsModalContent::Render() {
    ApplyAudioWorkerState();

    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Element body = selected_tab_ == static_cast<int>(Tab::Run)
        ? RenderRunTab()
        : RenderLibraryTab();

    return body |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg) |
        size(WIDTH, EQUAL, 108);
}

TtsModal::TtsModal(
    const Theme* theme,
    CloudTtsPipeline* pipeline,
    std::function<void()> prepare_current_file)
    : theme_(theme) {
    content_ = std::make_shared<TtsModalContent>(
        theme_,
        pipeline,
        std::move(prepare_current_file));
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

} // namespace textlt
