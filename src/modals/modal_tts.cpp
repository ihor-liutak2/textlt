#include "modal_tts.hpp"

#include <algorithm>
#include <cctype>
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
        text(value) | color(theme.modal_text_color),
    });
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
            ftxui::Element button = ftxui::text(BracketLabel(state.label));
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
    content_->RefreshLibrary();
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
