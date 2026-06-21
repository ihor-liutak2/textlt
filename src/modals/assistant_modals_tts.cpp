#include "assistant_modals.hpp"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

#include <curl/curl.h>

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
        text(value) | color(theme.modal_text_color),
    });
}

bool PiperVoiceInstalled(const Json& voice) {
    using namespace assistant_modal_detail;

    const std::string model_path = JsonString(voice, "model_path");
    const std::string config_path = JsonString(voice, "config_path");
    if (model_path.empty() || config_path.empty()) {
        return false;
    }

    const std::filesystem::path models_directory =
        UserDataDirectory() / "piper" / "models";
    std::error_code error;
    return std::filesystem::exists(models_directory / model_path, error) &&
           std::filesystem::exists(models_directory / config_path, error);
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

std::string FormatRatio(double ratio) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(0)
           << std::max(0.0, std::min(1.0, ratio)) * 100.0 << "%";
    return output.str();
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
    return BookDisplayTitle(book) + " | " + FormatRatio(book.progress_ratio);
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

bool FindSelectedPiperVoice(const std::string& selected_language,
                            int selected_voice,
                            Json* selected) {
    using namespace assistant_modal_detail;

    Json root;
    if (LoadUserRegistryJson(RegistryKind::Piper, &root) != RegistryLoadResult::Loaded) {
        return false;
    }

    const auto voices = root.find("voices");
    if (voices == root.end() || !voices->is_array()) {
        return false;
    }

    int visible_index = 0;
    for (const Json& voice : *voices) {
        if (!voice.is_object()) {
            continue;
        }
        const std::string language =
            JsonString(voice, "language_name", JsonString(voice, "language_code"));
        const std::string country = JsonString(voice, "country");
        const std::string key = country.empty() ? language : language + " - " + country;
        if (key != selected_language) {
            continue;
        }
        if (visible_index == selected_voice) {
            *selected = voice;
            return true;
        }
        ++visible_index;
    }
    return false;
}

std::string SelectedPiperLanguage(const std::vector<std::string>& labels,
                                  int selected_language) {
    return selected_language >= 0 &&
            selected_language < static_cast<int>(labels.size())
        ? labels[selected_language]
        : "";
}

int SelectedPiperVoiceCount(const std::vector<std::string>& labels,
                            int selected_voice) {
    if (selected_voice < 0 ||
        selected_voice >= static_cast<int>(labels.size()) ||
        labels[selected_voice] == "No voices") {
        return 0;
    }
    return 1;
}

std::vector<Json> SelectedInstalledPiperVoices(const std::string& selected_language,
                                               int selected_voice,
                                               const std::vector<std::string>& labels) {
    std::vector<Json> selected;
    if (SelectedPiperVoiceCount(labels, selected_voice) == 0) {
        return selected;
    }

    Json voice;
    if (FindSelectedPiperVoice(selected_language, selected_voice, &voice) &&
        PiperVoiceInstalled(voice)) {
        selected.push_back(std::move(voice));
    }
    return selected;
}

size_t WriteFileCallback(char* data, size_t size, size_t count, void* user_data) {
    FILE* file = static_cast<FILE*>(user_data);
    return std::fwrite(data, size, count, file);
}

struct PiperDownloadContext {
    std::atomic_bool* cancel = nullptr;
    std::mutex* mutex = nullptr;
    unsigned long long* downloaded_bytes = nullptr;
    unsigned long long* total_bytes = nullptr;
    float* progress_ratio = nullptr;
    std::function<void()>* request_redraw = nullptr;
};

int PiperProgressCallback(void* client,
                          curl_off_t total,
                          curl_off_t downloaded,
                          curl_off_t,
                          curl_off_t) {
    auto* context = static_cast<PiperDownloadContext*>(client);
    if (!context || !context->cancel || !context->mutex) {
        return 0;
    }
    if (*context->cancel) {
        return 1;
    }

    {
        std::lock_guard<std::mutex> lock(*context->mutex);
        *context->downloaded_bytes =
            downloaded > 0 ? static_cast<unsigned long long>(downloaded) : 0;
        if (total > 0) {
            *context->total_bytes = static_cast<unsigned long long>(total);
            *context->progress_ratio =
                static_cast<float>(downloaded) / static_cast<float>(total);
        } else if (*context->total_bytes > 0) {
            *context->progress_ratio =
                static_cast<float>(*context->downloaded_bytes) /
                static_cast<float>(*context->total_bytes);
        } else {
            *context->progress_ratio = downloaded > 0 ? 0.05f : 0.0f;
        }
    }
    if (context->request_redraw && *context->request_redraw) {
        (*context->request_redraw)();
    }
    return 0;
}

bool DownloadPiperFile(const std::string& url,
                       const std::filesystem::path& final_path,
                       const std::string& display_name,
                       unsigned long long expected_size,
                       std::mutex& state_mutex,
                       std::atomic_bool& cancel,
                       std::string& current_file,
                       unsigned long long& downloaded_bytes,
                       unsigned long long& total_bytes,
                       float& progress_ratio,
                       std::function<void()>& request_redraw,
                       std::string* error_message) {
    using namespace assistant_modal_detail;

    CreateDirectory(final_path.parent_path());
    CreateDirectory(DownloadCacheDirectory());
    const std::filesystem::path part_path =
        DownloadCacheDirectory() / (final_path.filename().string() + ".part");

    std::error_code error;
    std::filesystem::remove(part_path, error);

    {
        std::lock_guard<std::mutex> lock(state_mutex);
        current_file = display_name;
        downloaded_bytes = 0;
        total_bytes = expected_size;
        progress_ratio = 0.0f;
    }
    if (request_redraw) {
        request_redraw();
    }

    FILE* file = std::fopen(part_path.string().c_str(), "wb");
    if (!file) {
        *error_message = "Could not create .part file";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(file);
        std::filesystem::remove(part_path, error);
        *error_message = "Could not initialize download";
        return false;
    }

    PiperDownloadContext context{
        &cancel,
        &state_mutex,
        &downloaded_bytes,
        &total_bytes,
        &progress_ratio,
        &request_redraw,
    };
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "textlt/1.0");
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, PiperProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context);

    const CURLcode result = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    const bool close_ok = std::fclose(file) == 0;

    if (result != CURLE_OK || !close_ok) {
        std::filesystem::remove(part_path, error);
        *error_message = cancel.load()
            ? "Download cancelled"
            : "Download failed";
        return false;
    }

    std::filesystem::rename(part_path, final_path, error);
    if (error) {
        std::filesystem::remove(final_path, error);
        error.clear();
        std::filesystem::rename(part_path, final_path, error);
    }
    if (error) {
        std::filesystem::remove(part_path, error);
        *error_message = "Could not save downloaded file";
        return false;
    }
    return true;
}

} // namespace

TtsModalContent::TtsModalContent(
    const Theme* theme,
    CloudTtsPipeline* pipeline,
    std::function<void()> prepare_current_file)
    : theme_(theme),
      pipeline_(pipeline),
      prepare_current_file_(std::move(prepare_current_file)) {
    auto make_button = [this](std::string label, std::function<void()> on_click) {
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        option.label = std::move(label);
        option.on_click = std::move(on_click);
        option.transform = [this](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            ftxui::Element button =
                ftxui::text(assistant_modal_detail::BracketLabel(state.label));
            if (state.focused || state.active) {
                return button |
                    ftxui::bgcolor(theme.modal_selected_item_bg) |
                    ftxui::color(theme.modal_selected_item_fg);
            }
            return button | ftxui::color(theme.modal_accent);
        };
        return ftxui::Button(option);
    };

    refresh_library_button_ =
        make_button("Refresh library", [this] { RefreshLibrary(); });
    prepare_current_file_button_ = make_button("Prepare current file", [this] {
        if (!prepare_current_file_) {
            status_ = "TODO: current document is not available to TTS UI";
            return;
        }
        prepare_current_file_();
        status_ = "Queued current file preparation";
    });
    save_metadata_button_ =
        make_button("Save metadata", [this] { SaveSelectedMetadata(); });
    info_button_ =
        make_button("Info", [this] { ShowSelectedBookInfo(); });

    ftxui::MenuOption menu_option = ftxui::MenuOption::Vertical();
    menu_option.on_change = [this] { SyncMetadataFieldsFromSelection(); };
    menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(" " + state.label + " ");
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return row | ftxui::color(theme.modal_text_color);
    };
    library_menu_ =
        ftxui::Menu(&library_book_labels_, &selected_library_book_, menu_option);
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
                ftxui::color(theme.modal_selected_item_fg);
        }
        return row | ftxui::color(theme.modal_text_color);
    };
    language_menu_ = ftxui::Menu(&language_labels_, &selected_language_, language_option);
    ftxui::MenuOption voice_option = ftxui::MenuOption::Vertical();
    voice_option.entries_option.transform = language_option.entries_option.transform;
    piper_voice_menu_ =
        ftxui::Menu(&piper_voice_labels_, &selected_piper_voice_, voice_option);
    controls_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            refresh_library_button_,
            prepare_current_file_button_,
        }),
        library_menu_,
        info_button_,
        title_input_,
        author_input_,
        series_input_,
        genre_input_,
        series_index_input_,
        language_menu_,
        piper_voice_menu_,
        save_metadata_button_,
    });
    renderer_ = ftxui::Renderer(controls_, [this] { return Render(); });
    RefreshLibrary();
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
        return;
    }

    library_books_ = pipeline_->ListLocalBooks();
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
        SyncMetadataFieldsFromSelection();
    } else {
        status_ = "Loaded " + std::to_string(library_books_.size()) + " prepared books";
        SyncMetadataFieldsFromSelection();
    }
}

void TtsModalContent::RebuildMetadataOptions() {
    std::set<std::string> series_values;
    std::set<std::string> genre_values;
    for (const CloudTtsPipeline::BookInfo& book : library_books_) {
        if (!book.series.empty()) {
            series_values.insert(book.series);
        }
        if (!book.genre.empty()) {
            genre_values.insert(book.genre);
        }
    }
    known_series_.assign(series_values.begin(), series_values.end());
    known_genres_.assign(genre_values.begin(), genre_values.end());

    language_codes_ = {"unknown"};
    language_labels_ = {"Unknown"};

    Json root;
    if (assistant_modal_detail::LoadUserRegistryJson(
            assistant_modal_detail::RegistryKind::Piper,
            &root) == assistant_modal_detail::RegistryLoadResult::Loaded) {
        std::set<std::string> seen_codes = {"unknown"};
        const auto voices = root.find("voices");
        if (voices != root.end() && voices->is_array()) {
            for (const Json& voice : *voices) {
                if (!voice.is_object()) {
                    continue;
                }
                const std::string code = JsonString(voice, "language_code");
                if (code.empty() || !seen_codes.insert(code).second) {
                    continue;
                }
                const std::string name = JsonString(voice, "language_name", code);
                const std::string country = JsonString(voice, "country");
                const std::string label =
                    (country.empty() ? name : name + " - " + country) + " (" + code + ")";
                language_codes_.push_back(code);
                language_labels_.push_back(label);
            }
        }
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

    Json root;
    if (assistant_modal_detail::LoadUserRegistryJson(
            assistant_modal_detail::RegistryKind::Piper,
            &root) != assistant_modal_detail::RegistryLoadResult::Loaded) {
        piper_voice_labels_.push_back("Piper registry not loaded");
        return;
    }

    const auto voices = root.find("voices");
    if (voices != root.end() && voices->is_array()) {
        for (const Json& voice : *voices) {
            if (!voice.is_object() || JsonString(voice, "language_code") != language) {
                continue;
            }
            const std::string id = JsonString(voice, "id");
            if (id.empty()) {
                continue;
            }
            const bool installed = PiperVoiceInstalled(voice);
            std::string label = id;
            const std::string quality = JsonString(voice, "quality");
            const std::string speaker = JsonString(voice, "speaker");
            if (!speaker.empty()) {
                label += " | " + speaker;
            }
            if (!quality.empty()) {
                label += " | " + quality;
            }
            label += installed ? " | installed" : " | not installed";
            piper_voice_ids_.push_back(id);
            piper_voice_labels_.push_back(label);
            piper_voice_installed_.push_back(installed);
        }
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

void TtsModalContent::ShowSelectedBookInfo() {
    if (library_books_.empty() ||
        selected_library_book_ < 0 ||
        selected_library_book_ >= static_cast<int>(library_books_.size())) {
        show_selected_book_info_ = false;
        status_ = "No prepared book selected";
        return;
    }
    show_selected_book_info_ = true;
}

ftxui::Element TtsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Elements rows = {
        text(" TTS Library") | bold | color(theme.modal_text_color),
        hbox({
            refresh_library_button_->Render(),
            text(" "),
            prepare_current_file_button_->Render(),
        }),
        library_menu_->Render() | border,
    };

    if (!library_books_.empty() &&
        selected_library_book_ >= 0 &&
        selected_library_book_ < static_cast<int>(library_books_.size())) {
        const CloudTtsPipeline::BookInfo& book =
            library_books_[selected_library_book_];
        rows.push_back(text(" Editable metadata") | bold | color(theme.modal_text_color));
        rows.push_back(hbox({
            text(" Title: ") | bold | color(theme.modal_accent),
            title_input_->Render() | flex,
        }));
        rows.push_back(hbox({
            text(" Author: ") | bold | color(theme.modal_accent),
            author_input_->Render() | flex,
        }));
        rows.push_back(hbox({
            text(" Series: ") | bold | color(theme.modal_accent),
            series_input_->Render() | flex,
        }));
        const std::vector<std::string> series_suggestions =
            FilterSuggestions(known_series_, metadata_series_);
        if (!series_suggestions.empty()) {
            rows.push_back(text(" Suggestions: " + series_suggestions.front()) |
                           dim |
                           color(theme.modal_text_color));
            for (size_t index = 1; index < series_suggestions.size(); ++index) {
                rows.back() = hbox({
                    rows.back(),
                    text(", " + series_suggestions[index]) |
                        dim |
                        color(theme.modal_text_color),
                });
            }
        }
        rows.push_back(hbox({
            text(" Genre: ") | bold | color(theme.modal_accent),
            genre_input_->Render() | flex,
        }));
        const std::vector<std::string> genre_suggestions =
            FilterSuggestions(known_genres_, metadata_genre_);
        if (!genre_suggestions.empty()) {
            rows.push_back(text(" Suggestions: " + genre_suggestions.front()) |
                           dim |
                           color(theme.modal_text_color));
            for (size_t index = 1; index < genre_suggestions.size(); ++index) {
                rows.back() = hbox({
                    rows.back(),
                    text(", " + genre_suggestions[index]) |
                        dim |
                        color(theme.modal_text_color),
                });
            }
        }
        rows.push_back(hbox({
            text(" Series index: ") | bold | color(theme.modal_accent),
            series_index_input_->Render() | flex,
        }));
        rows.push_back(text(" Language") | bold | color(theme.modal_accent));
        rows.push_back(language_menu_->Render() | border);
        rows.push_back(text(" Voice") | bold | color(theme.modal_accent));
        const std::string selected_language =
            selected_language_ >= 0 &&
                    selected_language_ < static_cast<int>(language_codes_.size())
                ? language_codes_[selected_language_]
                : "unknown";
        if (selected_language == "unknown") {
            rows.push_back(text(" Select book language first") |
                           color(theme.modal_text_color) |
                           border);
        } else {
            rows.push_back(piper_voice_menu_->Render() | border);
            if (selected_piper_voice_ >= 0 &&
                selected_piper_voice_ < static_cast<int>(piper_voice_installed_.size())) {
                rows.push_back(StatusLine(
                    "Voice",
                    piper_voice_installed_[selected_piper_voice_]
                        ? "installed"
                        : "not installed",
                    theme));
            }
        }
        rows.push_back(save_metadata_button_->Render());
        rows.push_back(info_button_->Render());
        if (show_selected_book_info_) {
            rows.push_back(text(" Read-only source") | bold | color(theme.modal_text_color));
            rows.push_back(StatusLine("Book id", book.book_id, theme));
            rows.push_back(StatusLine("Source", book.source_file_name, theme));
            rows.push_back(StatusLine("Source path", book.source_path.string(), theme));
            rows.push_back(StatusLine(
                "File size",
                FormatBytes(static_cast<unsigned long long>(book.file_size)) + " bytes",
                theme));
            rows.push_back(StatusLine("Modified time", std::to_string(book.modified_time), theme));
            rows.push_back(StatusLine(
                "Last cursor line",
                std::to_string(book.last_cursor_line),
                theme));
            rows.push_back(StatusLine("Total chunks", std::to_string(book.total_chunks), theme));
            rows.push_back(StatusLine(
                "Chunks",
                "ready " + std::to_string(book.ready_chunks) +
                    " / prepared " + std::to_string(book.prepared_chunks) +
                    " / failed " + std::to_string(book.failed_chunks) +
                    " / played " + std::to_string(book.played_chunks),
                theme));
            rows.push_back(StatusLine("Progress", FormatRatio(book.progress_ratio), theme));
        }
    }

    return vbox(std::move(rows)) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

void AssistantSettingsModalContent::LoadPiperRegistry() {
    using namespace assistant_modal_detail;

    Json root;
    const RegistryLoadResult load_result = LoadUserRegistryJson(RegistryKind::Piper, &root);
    std::map<std::string, std::vector<std::string>> voices_by_language;
    if (load_result == RegistryLoadResult::Loaded) {
        const auto voices = root.find("voices");
        if (voices != root.end() && voices->is_array()) {
            for (const Json& voice : *voices) {
                if (!voice.is_object()) {
                    continue;
                }
                const std::string code = JsonString(voice, "language_code");
                const std::string language = JsonString(voice, "language_name", code);
                const std::string country = JsonString(voice, "country");
                const std::string key = country.empty() ? language : language + " - " + country;
                voices_by_language[key].push_back("");
            }
        }
    }

    tts_language_labels_.clear();
    for (const auto& entry : voices_by_language) {
        tts_language_labels_.push_back(entry.first);
    }
    if (tts_language_labels_.empty()) {
        tts_language_labels_.push_back("No languages");
        if (load_result == RegistryLoadResult::Missing) {
            tts_status_ = "Registry not loaded";
        } else if (load_result == RegistryLoadResult::ParseFailed) {
            tts_status_ = "Failed to parse registry";
        } else {
            tts_status_ = "Registry loaded, no items found";
        }
    } else if (tts_status_.find("TODO:") != 0) {
        tts_status_ = "Registry loaded";
    }
    selected_tts_language_ = 0;
    RebuildTtsVoices();
}

void AssistantSettingsModalContent::RebuildTtsVoices() {
    using namespace assistant_modal_detail;

    tts_delete_confirm_visible_ = false;
    tts_delete_pending_voices_.clear();

    Json root;
    const RegistryLoadResult load_result = LoadUserRegistryJson(RegistryKind::Piper, &root);
    const std::string selected_language =
        selected_tts_language_ >= 0 &&
                selected_tts_language_ < static_cast<int>(tts_language_labels_.size())
            ? tts_language_labels_[selected_tts_language_]
            : "";
    tts_voice_labels_.clear();
    if (load_result == RegistryLoadResult::Loaded) {
        const auto voices = root.find("voices");
        if (voices != root.end() && voices->is_array()) {
            for (const Json& voice : *voices) {
                if (!voice.is_object()) {
                    continue;
                }
                const std::string language =
                    JsonString(voice, "language_name", JsonString(voice, "language_code"));
                const std::string country = JsonString(voice, "country");
                const std::string key = country.empty() ? language : language + " - " + country;
                if (key != selected_language) {
                    continue;
                }
                std::string label = JsonString(voice, "id");
                const std::string quality = JsonString(voice, "quality");
                label += " | " + (quality.empty() ? "unknown" : quality);
                label += PiperVoiceInstalled(voice) ? " | installed" : " | not installed";
                tts_voice_labels_.push_back(label);
            }
        }
    }
    if (tts_voice_labels_.empty()) {
        tts_voice_labels_.push_back("No voices");
    }
    selected_tts_voice_ = 0;
}

void AssistantSettingsModalContent::StartTtsVoiceDownload() {
    using namespace assistant_modal_detail;

    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        if (tts_downloading_) {
            return;
        }
    }
    if (tts_download_thread_.joinable()) {
        tts_download_thread_.join();
    }

    const std::string selected_language =
        selected_tts_language_ >= 0 &&
                selected_tts_language_ < static_cast<int>(tts_language_labels_.size())
            ? tts_language_labels_[selected_tts_language_]
            : "";

    Json voice;
    if (!FindSelectedPiperVoice(selected_language, selected_tts_voice_, &voice)) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Select a voice first";
        tts_download_visible_ = false;
        return;
    }

    const std::string model_path = JsonString(voice, "model_path");
    const std::string config_path = JsonString(voice, "config_path");
    const unsigned long long model_size =
        static_cast<unsigned long long>(JsonSize(voice, "model_size", 0));
    const unsigned long long config_size =
        static_cast<unsigned long long>(JsonSize(voice, "config_size", 0));
    if (model_path.empty() || config_path.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Voice registry entry is incomplete";
        tts_download_visible_ = false;
        return;
    }

    Json root;
    if (LoadUserRegistryJson(RegistryKind::Piper, &root) != RegistryLoadResult::Loaded) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Registry not loaded";
        tts_download_visible_ = false;
        return;
    }

    const std::string base_url = JsonString(root, "base_url");
    if (base_url.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Registry has no base URL";
        tts_download_visible_ = false;
        return;
    }

    auto make_url = [base_url](const std::string& path) {
        if (base_url.back() == '/' || (!path.empty() && path.front() == '/')) {
            return base_url + path;
        }
        return base_url + "/" + path;
    };

    const std::filesystem::path models_directory =
        UserDataDirectory() / "piper" / "models";
    const std::filesystem::path model_final_path = models_directory / model_path;
    const std::filesystem::path config_final_path = models_directory / config_path;
    const std::string model_url = make_url(model_path);
    const std::string config_url = make_url(config_path);

    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_cancel_download_ = false;
        tts_downloading_ = true;
        tts_download_visible_ = true;
        tts_refresh_after_download_ = false;
        tts_download_current_file_ = std::filesystem::path(model_path).filename().string();
        tts_downloaded_bytes_ = 0;
        tts_total_bytes_ = 0;
        tts_progress_ratio_ = 0.0f;
        tts_status_ = "Downloading voice...";
    }
    RequestRedraw();

    tts_download_thread_ = std::thread([this,
                                        model_url,
                                        config_url,
                                        model_final_path,
                                        config_final_path,
                                        model_path,
                                        config_path,
                                        model_size,
                                        config_size] {
        std::string error_message;
        const bool model_ok = DownloadPiperFile(
            model_url,
            model_final_path,
            std::filesystem::path(model_path).filename().string(),
            model_size,
            tts_download_mutex_,
            tts_cancel_download_,
            tts_download_current_file_,
            tts_downloaded_bytes_,
            tts_total_bytes_,
            tts_progress_ratio_,
            request_redraw_,
            &error_message);
        bool config_ok = false;
        if (model_ok) {
            config_ok = DownloadPiperFile(
                config_url,
                config_final_path,
                std::filesystem::path(config_path).filename().string(),
                config_size,
                tts_download_mutex_,
                tts_cancel_download_,
                tts_download_current_file_,
                tts_downloaded_bytes_,
                tts_total_bytes_,
                tts_progress_ratio_,
                request_redraw_,
                &error_message);
        }

        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_downloading_ = false;
        if (model_ok && config_ok) {
            tts_status_ = "Voice downloaded";
            tts_download_visible_ = false;
            tts_progress_ratio_ = 1.0f;
            tts_refresh_after_download_ = true;
        } else {
            tts_download_visible_ = true;
            tts_status_ = error_message.empty()
                ? "Voice download failed"
                : "Voice download failed: " + error_message;
        }
        RequestRedraw();
    });
}

void AssistantSettingsModalContent::StartTtsVoiceDelete() {
    const std::string selected_language =
        SelectedPiperLanguage(tts_language_labels_, selected_tts_language_);
    std::vector<Json> voices =
        SelectedInstalledPiperVoices(selected_language, selected_tts_voice_, tts_voice_labels_);
    if (voices.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_delete_confirm_visible_ = false;
        tts_delete_pending_voices_.clear();
        tts_status_ = "No installed voice selected";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_delete_pending_voices_ = std::move(voices);
        tts_delete_confirm_visible_ = true;
        tts_download_visible_ = false;
    }
    if (tts_confirm_delete_button_) {
        tts_confirm_delete_button_->TakeFocus();
    }
}

void AssistantSettingsModalContent::ConfirmTtsVoiceDelete() {
    using namespace assistant_modal_detail;

    std::vector<Json> voices;
    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        voices = tts_delete_pending_voices_;
    }
    if (voices.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_delete_confirm_visible_ = false;
        tts_status_ = "No installed voice selected";
        return;
    }

    const std::filesystem::path models_directory =
        UserDataDirectory() / "piper" / "models";
    for (const Json& voice : voices) {
        const std::string model_path = JsonString(voice, "model_path");
        const std::string config_path = JsonString(voice, "config_path");
        std::error_code error;
        if (!model_path.empty()) {
            std::filesystem::remove(models_directory / model_path, error);
        }
        error.clear();
        if (!config_path.empty()) {
            std::filesystem::remove(models_directory / config_path, error);
        }
    }

    const size_t deleted_count = voices.size();
    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_delete_confirm_visible_ = false;
        tts_delete_pending_voices_.clear();
    }
    RebuildTtsVoices();
    selected_tts_voice_ = -1;
    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = deleted_count == 1 ? "Voice deleted" : "Voices deleted";
    }
}

void AssistantSettingsModalContent::CancelTtsVoiceDelete() {
    std::lock_guard<std::mutex> lock(tts_download_mutex_);
    tts_delete_confirm_visible_ = false;
    tts_delete_pending_voices_.clear();
}

void AssistantSettingsModalContent::TestTtsVoice() {
    const int selected_count =
        SelectedPiperVoiceCount(tts_voice_labels_, selected_tts_voice_);
    if (selected_count == 0) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "No voice selected";
        return;
    }
    if (selected_count > 1) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Select one voice to test";
        return;
    }

    Json voice;
    const std::string selected_language =
        SelectedPiperLanguage(tts_language_labels_, selected_tts_language_);
    if (!FindSelectedPiperVoice(selected_language, selected_tts_voice_, &voice)) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "No voice selected";
        return;
    }
    if (!PiperVoiceInstalled(voice)) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Voice is not installed";
        return;
    }

    std::lock_guard<std::mutex> lock(tts_download_mutex_);
    tts_status_ = "TODO: Piper test playback not implemented";
}

void AssistantSettingsModalContent::ApplyTtsDownloadCompletion() {
    bool should_refresh = false;
    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        should_refresh = tts_refresh_after_download_;
        tts_refresh_after_download_ = false;
    }
    if (should_refresh) {
        RebuildTtsVoices();
    }
}

void AssistantSettingsModalContent::RequestRedraw() const {
    if (request_redraw_) {
        request_redraw_();
    }
}

ftxui::Element AssistantSettingsModalContent::RenderTtsTab(const Theme& theme) {
    using namespace ftxui;
    ApplyTtsDownloadCompletion();

    bool show_download_progress = false;
    bool show_delete_confirmation = false;
    std::string current_file;
    unsigned long long downloaded_bytes = 0;
    unsigned long long total_bytes = 0;
    float progress_ratio = 0.0f;
    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        show_download_progress = tts_download_visible_;
        show_delete_confirmation = tts_delete_confirm_visible_;
        current_file = tts_download_current_file_;
        downloaded_bytes = tts_downloaded_bytes_;
        total_bytes = tts_total_bytes_;
        progress_ratio = tts_progress_ratio_;
    }

    Elements rows = {
        hbox({
            fetch_tts_button_->Render(),
            text(" "),
            tts_download_button_->Render(),
            text(" "),
            tts_delete_button_->Render(),
            text(" "),
            tts_test_button_->Render(),
        }),
    };
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(text(" Language") | bold | color(theme.modal_text_color));
    rows.push_back(tts_language_menu_->Render() | border);
    if (show_download_progress) {
        progress_ratio = std::max(0.0f, std::min(1.0f, progress_ratio));
        rows.push_back(separator() | color(theme.modal_border));
        rows.push_back(text(" Download progress") | bold | color(theme.modal_text_color));
        rows.push_back(text(" " + current_file) | color(theme.modal_text_color));
        std::string byte_text = FormatBytes(downloaded_bytes) + " bytes";
        if (total_bytes > 0) {
            byte_text = FormatBytes(downloaded_bytes) + " / " +
                        FormatBytes(total_bytes) + " bytes";
        }
        const int percent =
            static_cast<int>(progress_ratio * 100.0f + 0.5f);
        rows.push_back(hbox({
            text(" " + byte_text) | color(theme.modal_text_color),
            filler(),
            text(std::to_string(percent) + "% ") |
                color(theme.modal_text_color),
        }));
        rows.push_back(gauge(progress_ratio) | border);
    }
    if (show_delete_confirmation) {
        rows.push_back(separator() | color(theme.modal_border));
        rows.push_back(text(" Delete selected voice files?") |
                       bold |
                       color(theme.modal_text_color));
        rows.push_back(hbox({
            tts_confirm_delete_button_->Render(),
            text(" "),
            tts_cancel_delete_button_->Render(),
        }));
    }
    rows.push_back(text(" Voices") | bold | color(theme.modal_text_color));
    rows.push_back(tts_voice_menu_->Render() | border);
    return vbox(std::move(rows)) | border;
}

} // namespace textlt
