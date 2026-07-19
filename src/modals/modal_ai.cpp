#include "modals/modal_ai.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ui_button.hpp"

namespace textlt {
namespace {

ftxui::Component MakeButton(
    const Theme** theme,
    std::string label,
    std::function<void()> on_click,
    ButtonRole role = ButtonRole::Default,
    std::function<bool()> selected = {}) {
    ButtonSpec base = ButtonSpecFromLabel(
        std::move(label), role, ButtonVariant::AccentEdges, ButtonSize::Compact);
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(base);
    option.on_click = std::move(on_click);
    option.transform = [theme, base, selected = std::move(selected)](
                           const ftxui::EntryState& state) mutable {
        const Theme& resolved = theme && *theme ? **theme : FallbackTheme();
        base.selected = selected && selected();
        return RenderModalFlatButton(resolved, base, state.focused || state.active);
    };
    return ftxui::Button(option);
}

std::string ActionLabel(AiActionType action) {
    return action == AiActionType::Translate ? "Translation" : "Editing";
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<std::string> SupportedLanguages() {
    return {
        "Afrikaans", "Albanian", "Amharic", "Arabic", "Armenian", "Azerbaijani",
        "Basque", "Belarusian", "Bengali", "Bosnian", "Bulgarian", "Burmese",
        "Catalan", "Chinese", "Croatian", "Czech", "Danish", "Dutch", "English",
        "Estonian", "Finnish", "French", "Galician", "Georgian", "German", "Greek",
        "Gujarati", "Hebrew", "Hindi", "Hungarian", "Icelandic", "Indonesian",
        "Irish", "Italian", "Japanese", "Kannada", "Kazakh", "Korean", "Latvian",
        "Lithuanian", "Macedonian", "Malay", "Malayalam", "Marathi", "Mongolian",
        "Nepali", "Norwegian", "Persian", "Polish", "Portuguese", "Punjabi",
        "Romanian", "Russian", "Serbian", "Slovak", "Slovenian", "Spanish",
        "Swahili", "Swedish", "Tamil", "Telugu", "Thai", "Turkish", "Ukrainian",
        "Urdu", "Uzbek", "Vietnamese", "Welsh"
    };
}

std::string CanonicalLanguage(
    const std::vector<std::string>& languages,
    const std::string& value) {
    const std::string key = LowerAscii(value);
    const auto found = std::find_if(languages.begin(), languages.end(), [&](const std::string& language) {
        return LowerAscii(language) == key;
    });
    return found == languages.end() ? std::string{} : *found;
}

std::string TailUtf8(const std::string& value, size_t character_count) {
    if (value.empty()) {
        return {};
    }

    size_t end = value.size();
    size_t sequence_start = end - 1;
    while (sequence_start > 0 &&
           (static_cast<unsigned char>(value[sequence_start]) & 0xC0U) == 0x80U) {
        --sequence_start;
    }
    const unsigned char first = static_cast<unsigned char>(value[sequence_start]);
    const size_t expected = (first & 0xE0U) == 0xC0U ? 2
        : (first & 0xF0U) == 0xE0U ? 3
        : (first & 0xF8U) == 0xF0U ? 4
        : 1;
    if (sequence_start + expected > end) {
        end = sequence_start;
    }

    size_t position = end;
    size_t count = 0;
    while (position > 0 && count < character_count) {
        --position;
        while (position > 0 &&
               (static_cast<unsigned char>(value[position]) & 0xC0U) == 0x80U) {
            --position;
        }
        ++count;
    }
    return value.substr(position, end - position);
}

ftxui::Element LabelValue(
    const std::string& label,
    const std::string& value,
    const Theme& theme) {
    using namespace ftxui;
    return hbox({
        text(" " + label + ": ") | bold | color(theme.modal_accent),
        paragraph(value.empty() ? "-" : value) | color(theme.modal_text_color) | flex,
    });
}

} // namespace

AiActionsModalContent::AiActionsModalContent(
    const Theme* theme,
    EditorConfig* config,
    CaptureAiTargetCallback capture_target,
    ApplyAiTargetCallback apply_target,
    std::function<void()> request_redraw,
    std::function<void(const std::string&)> notify_status,
    std::function<void(bool, const std::string&)> quick_completion)
    : theme_(theme),
      config_(config),
      capture_target_(std::move(capture_target)),
      apply_target_(std::move(apply_target)),
      request_redraw_(std::move(request_redraw)),
      notify_status_(std::move(notify_status)),
      quick_completion_(std::move(quick_completion)),
      languages_(SupportedLanguages()) {
    ftxui::InputOption source_option;
    source_option.multiline = false;
    source_option.cursor_position = &source_language_cursor_;
    source_option.on_change = [this] { FilterLanguages(true); };
    source_option.on_enter = [this] { CommitLanguageSelection(true); };
    source_option.transform = [this](ftxui::InputState state) {
        const Theme& resolved = theme_ ? *theme_ : FallbackTheme();
        return resolved.InputTransform(std::move(state));
    };
    source_language_input_ = ftxui::Input(&source_language_, "English", source_option);

    ftxui::InputOption target_option = source_option;
    target_option.cursor_position = &target_language_cursor_;
    target_option.on_change = [this] { FilterLanguages(false); };
    target_option.on_enter = [this] { CommitLanguageSelection(false); };
    target_language_input_ = ftxui::Input(&target_language_, "Ukrainian", target_option);

    ftxui::MenuOption source_menu_option = ftxui::MenuOption::Vertical();
    source_menu_option.on_change = [this] { CommitLanguageSelection(true); };
    source_menu_option.on_enter = [this] { CommitLanguageSelection(true); };
    source_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& resolved = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(" " + state.label + " ");
        if (state.focused || state.active) {
            return row | ftxui::bgcolor(resolved.modal_selected_item_bg) |
                ftxui::color(resolved.modal_selected_item_fg);
        }
        return row | ftxui::color(resolved.modal_text_color);
    };
    source_language_menu_ = ftxui::Menu(
        &filtered_source_languages_, &selected_source_language_, source_menu_option);

    ftxui::MenuOption target_menu_option = source_menu_option;
    target_menu_option.on_change = [this] { CommitLanguageSelection(false); };
    target_menu_option.on_enter = [this] { CommitLanguageSelection(false); };
    target_language_menu_ = ftxui::Menu(
        &filtered_target_languages_, &selected_target_language_, target_menu_option);

    conversational_button_ = MakeButton(
        &theme_,
        "Conversational",
        [this] { SetEditStyle(AiEditStyle::Conversational); },
        ButtonRole::Toggle,
        [this] { return edit_style_ == AiEditStyle::Conversational; });
    business_button_ = MakeButton(
        &theme_,
        "Business",
        [this] { SetEditStyle(AiEditStyle::Business); },
        ButtonRole::Toggle,
        [this] { return edit_style_ == AiEditStyle::Business; });

    ftxui::CheckboxOption checkbox_option = ftxui::CheckboxOption::Simple();
    checkbox_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& resolved = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element item = ftxui::text(
            std::string(state.state ? "[X] " : "[ ] ") + state.label);
        if (state.focused || state.active) {
            return item | ftxui::bgcolor(resolved.modal_selected_item_bg) |
                ftxui::color(resolved.modal_selected_item_fg);
        }
        return item | ftxui::color(resolved.modal_text_color);
    };
    whole_document_checkbox_ = ftxui::Checkbox(
        "Whole document",
        &whole_document_,
        checkbox_option);

    translate_button_ = MakeButton(
        &theme_, "1 Translate", [this] {
            StartAction(AiActionType::Translate, false);
        },
        ButtonRole::Primary);
    edit_button_ = MakeButton(
        &theme_, "2 Edit", [this] {
            StartAction(AiActionType::Edit, false);
        },
        ButtonRole::Primary);
    close_info_button_ = MakeButton(
        &theme_, "Close", [this] { CloseInfo(); }, ButtonRole::Cancel);

    auto source_language_group = ftxui::Container::Vertical({
        source_language_input_, source_language_menu_,
    });
    auto target_language_group = ftxui::Container::Vertical({
        target_language_input_, target_language_menu_,
    });
    auto main_content = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({source_language_group, target_language_group}),
        ftxui::Container::Horizontal({conversational_button_, business_button_}),
        whole_document_checkbox_,
        ftxui::Container::Horizontal({translate_button_, edit_button_}),
    });
    auto info_content = ftxui::Container::Vertical({close_info_button_});
    auto panels = ftxui::Container::Tab({main_content, info_content}, &active_panel_);
    container_ = ftxui::CatchEvent(panels, [this](ftxui::Event event) {
        if (info_visible_ && event == ftxui::Event::Escape) {
            CloseInfo();
            return true;
        }
        return HandleShortcut(std::move(event));
    });
    RefreshFromConfig();
}

AiActionsModalContent::~AiActionsModalContent() {
    StopQuickReadinessCheck();
    Stop();
    PersistOptions();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void AiActionsModalContent::RefreshFromConfig() {
    if (!config_) {
        return;
    }
    source_language_ = CanonicalLanguage(languages_, config_->ai_translation_source_language);
    if (source_language_.empty()) {
        source_language_ = "English";
    }
    target_language_ = CanonicalLanguage(languages_, config_->ai_translation_language);
    if (target_language_.empty()) {
        target_language_ = "Ukrainian";
    }
    source_language_cursor_ = static_cast<int>(source_language_.size());
    target_language_cursor_ = static_cast<int>(target_language_.size());
    edit_style_ = config_->ai_edit_style == "business"
        ? AiEditStyle::Business
        : AiEditStyle::Conversational;
    whole_document_ = config_->ai_whole_document;
    persisted_source_language_ = source_language_;
    persisted_target_language_ = target_language_;
    committed_source_language_ = source_language_;
    committed_target_language_ = target_language_;
    persisted_edit_style_ = edit_style_;
    persisted_whole_document_ = whole_document_;
    FilterLanguages(true);
    FilterLanguages(false);
}

void AiActionsModalContent::StopQuickReadinessCheck() {
    quick_readiness_cancel_.store(true);
    quick_readiness_control_.RequestStop();
    if (quick_readiness_worker_.joinable()) {
        quick_readiness_worker_.join();
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    quick_readiness_checking_ = false;
}

void AiActionsModalContent::StartQuickReadinessCheck() {
    StopQuickReadinessCheck();
    if (!config_) {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            quick_ready_ = false;
            quick_readiness_status_ = "Not ready — AI settings are unavailable";
        }
        RequestRedraw();
        return;
    }

    AiBackendSettings settings;
    settings.server_url = config_->ai_server_url;
    settings.provider = AiBackend::ProviderFromConfig(config_->ai_provider);
    settings.selected_model_key = config_->ai_selected_model_key;
    settings.timeout_seconds = 8;

    if (settings.selected_model_key.empty()) {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            quick_ready_ = false;
            quick_readiness_status_ = "Not ready — select a model in AI Settings";
        }
        RequestRedraw();
        return;
    }

    quick_readiness_cancel_.store(false);
    quick_readiness_control_.Reset();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        quick_readiness_checking_ = true;
        quick_ready_ = false;
        quick_readiness_status_ = "Checking AI backend...";
    }
    RequestRedraw();

    quick_readiness_worker_ = std::thread([this, settings] {
        const AiConnectionResult result = AiBackend(settings).CheckSelectedModelReady(
            &quick_readiness_cancel_, &quick_readiness_control_);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            quick_readiness_checking_ = false;
            if (quick_readiness_cancel_.load()) {
                quick_ready_ = false;
                quick_readiness_status_ = "Readiness check stopped";
            } else if (result.success) {
                quick_ready_ = true;
                quick_readiness_status_ = "Ready — " + result.provider_label;
            } else {
                quick_ready_ = false;
                quick_readiness_status_ = "Not ready — " +
                    (result.error.empty() ? std::string("backend check failed") : result.error);
            }
        }
        RequestRedraw();
    });
}

void AiActionsModalContent::PrepareQuickActions() {
    RefreshFromConfig();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (busy_) {
            return;
        }
    }
    StartQuickReadinessCheck();
}

AiQuickStatusSnapshot AiActionsModalContent::QuickStatus() const {
    AiQuickStatusSnapshot snapshot;
    snapshot.model_label = SelectedModelLabel();
    std::lock_guard<std::mutex> lock(state_mutex_);
    snapshot.busy = busy_;
    snapshot.stopping = operation_state_ == OperationState::Stopping;
    snapshot.checking = quick_readiness_checking_;
    snapshot.ready = quick_ready_ && !quick_readiness_checking_;
    snapshot.active_action = active_action_;
    if (snapshot.busy) {
        if (snapshot.stopping) {
            snapshot.status_label = "Stopping AI operation...";
        } else {
            snapshot.status_label = active_action_ == AiActionType::Translate
                ? "Working — translating current paragraph"
                : "Working — editing current paragraph";
        }
    } else {
        snapshot.status_label = quick_readiness_status_;
    }
    return snapshot;
}

void AiActionsModalContent::StopQuickAction() {
    Stop();
}

void AiActionsModalContent::FilterLanguages(bool source) {
    std::string& text = source ? source_language_ : target_language_;
    std::vector<std::string>& filtered = source
        ? filtered_source_languages_
        : filtered_target_languages_;
    int& selected = source ? selected_source_language_ : selected_target_language_;
    const std::string filter = LowerAscii(text);
    filtered.clear();
    for (const std::string& language : languages_) {
        if (filter.empty() || LowerAscii(language).find(filter) != std::string::npos) {
            filtered.push_back(language);
            if (filtered.size() == 8) {
                break;
            }
        }
    }
    if (filtered.empty()) {
        filtered.push_back("No matching language");
    }
    selected = 0;
}

void AiActionsModalContent::CommitLanguageSelection(bool source) {
    std::vector<std::string>& filtered = source
        ? filtered_source_languages_
        : filtered_target_languages_;
    int& selected = source ? selected_source_language_ : selected_target_language_;
    if (filtered.empty() || selected < 0 || static_cast<size_t>(selected) >= filtered.size() ||
        filtered[static_cast<size_t>(selected)] == "No matching language") {
        return;
    }
    std::string& value = source ? source_language_ : target_language_;
    int& cursor = source ? source_language_cursor_ : target_language_cursor_;
    value = filtered[static_cast<size_t>(selected)];
    cursor = static_cast<int>(value.size());
    if (source) {
        committed_source_language_ = value;
    } else {
        committed_target_language_ = value;
    }
}

void AiActionsModalContent::FinalizeLanguageInput(bool source) {
    std::string& value = source ? source_language_ : target_language_;
    int& cursor = source ? source_language_cursor_ : target_language_cursor_;
    std::string& committed = source
        ? committed_source_language_
        : committed_target_language_;
    std::vector<std::string>& filtered = source
        ? filtered_source_languages_
        : filtered_target_languages_;
    int& selected = source ? selected_source_language_ : selected_target_language_;

    const std::string canonical = CanonicalLanguage(languages_, value);
    if (!canonical.empty()) {
        value = canonical;
        committed = canonical;
    } else if (!filtered.empty() && selected >= 0 &&
               static_cast<size_t>(selected) < filtered.size() &&
               filtered[static_cast<size_t>(selected)] != "No matching language") {
        value = filtered[static_cast<size_t>(selected)];
        committed = value;
    } else {
        value = committed;
    }
    cursor = static_cast<int>(value.size());
    FilterLanguages(source);
}

void AiActionsModalContent::FinalizeUnfocusedLanguageInputs() {
    const bool source_focused =
        source_language_input_->Focused() || source_language_menu_->Focused();
    const bool target_focused =
        target_language_input_->Focused() || target_language_menu_->Focused();

    if (source_language_was_focused_ && !source_focused) {
        FinalizeLanguageInput(true);
    }
    if (target_language_was_focused_ && !target_focused) {
        FinalizeLanguageInput(false);
    }
    source_language_was_focused_ = source_focused;
    target_language_was_focused_ = target_focused;
}

bool AiActionsModalContent::ValidateLanguages(
    std::string& error,
    bool require_distinct) {
    FinalizeLanguageInput(true);
    FinalizeLanguageInput(false);
    const std::string source = CanonicalLanguage(languages_, source_language_);
    const std::string target = CanonicalLanguage(languages_, target_language_);
    if (source.empty()) {
        error = "Select the source language from the supported language list.";
        return false;
    }
    if (target.empty()) {
        error = "Select the target language from the supported language list.";
        return false;
    }
    if (require_distinct && source == target) {
        error = "Source and target languages must be different for translation.";
        return false;
    }
    source_language_ = source;
    target_language_ = target;
    source_language_cursor_ = static_cast<int>(source_language_.size());
    target_language_cursor_ = static_cast<int>(target_language_.size());
    return true;
}

void AiActionsModalContent::SetEditStyle(AiEditStyle style) {
    edit_style_ = style;
    PersistOptions();
}

bool AiActionsModalContent::PersistOptions() {
    if (!config_) {
        return false;
    }
    std::string language_error;
    if (!ValidateLanguages(language_error, false)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = language_error;
        return false;
    }
    if (persisted_source_language_ == source_language_ &&
        persisted_target_language_ == target_language_ &&
        persisted_edit_style_ == edit_style_ &&
        persisted_whole_document_ == whole_document_) {
        return true;
    }
    config_->ai_translation_source_language = source_language_;
    config_->ai_translation_language = target_language_;
    config_->ai_edit_style = edit_style_ == AiEditStyle::Business
        ? "business"
        : "conversational";
    config_->ai_whole_document = whole_document_;
    const bool saved = config_->Persist();
    if (saved) {
        persisted_source_language_ = source_language_;
        persisted_target_language_ = target_language_;
        persisted_edit_style_ = edit_style_;
        persisted_whole_document_ = whole_document_;
    } else {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "Could not save AI action settings.";
    }
    return saved;
}

bool AiActionsModalContent::StartAction(
    AiActionType action,
    bool force_current_paragraph,
    std::string* error_out) {
    auto reject = [&](std::string message) {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            status_ = message;
        }
        if (error_out) {
            *error_out = std::move(message);
        }
        RequestRedraw();
        return false;
    };

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (busy_) {
            const std::string message = "An AI request is already running.";
            status_ = message;
            if (error_out) {
                *error_out = message;
            }
            return false;
        }
    }
    StopQuickReadinessCheck();
    if (!PersistOptions()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (error_out) {
            *error_out = status_.empty()
                ? "Could not save AI action settings."
                : status_;
        }
        return false;
    }
    if (action == AiActionType::Translate) {
        std::string language_error;
        if (!ValidateLanguages(language_error, true)) {
            return reject(std::move(language_error));
        }
    }
    if (!config_ || config_->ai_selected_model_key.empty()) {
        return reject("Select an AI model in AI Settings first.");
    }
    if (!capture_target_) {
        return reject("AI text capture is unavailable.");
    }

    AiDocumentTarget target;
    std::string error;
    const bool whole_document = force_current_paragraph ? false : whole_document_;
    if (!capture_target_(whole_document, target, error)) {
        return reject(error.empty() ? "Could not read the target text." : error);
    }

    AiPromptRequest request;
    request.action = action;
    request.text = target.range.original_text;
    request.source_language = source_language_;
    request.target_language = target_language_;
    request.edit_style = edit_style_;
    AiBackendSettings settings;
    settings.server_url = config_->ai_server_url;
    settings.provider = AiBackend::ProviderFromConfig(config_->ai_provider);
    settings.selected_model_key = config_->ai_selected_model_key;
    settings.timeout_seconds = whole_document ? 300 : 180;
    settings.max_output_tokens = whole_document ? 2048 : 512;

    if (worker_.joinable()) {
        worker_.join();
    }
    cancel_requested_.store(false);
    command_control_.Reset();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        busy_ = true;
        active_action_ = action;
        operation_state_ = OperationState::Starting;
        discard_result_ = false;
        pending_result_ = false;
        progress_text_ = "Waiting for model output...";
        status_ = action == AiActionType::Translate
            ? "Translating from " + source_language_ + " to " + target_language_ + "..."
            : "Editing text...";
    }
    RequestRedraw();
    const bool quick_action = force_current_paragraph;
    worker_ = std::thread([this, settings, request, target = std::move(target), action, quick_action] {
        AiBackendResult result = AiBackend(settings).Run(
            request,
            &cancel_requested_,
            [this](const std::string& generated) {
                bool changed = false;
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    if (operation_state_ != OperationState::Stopping &&
                        !cancel_requested_.load()) {
                        operation_state_ = OperationState::Generating;
                        progress_text_ = TailUtf8(generated, 120);
                        changed = true;
                    }
                }
                if (changed) {
                    RequestRedraw();
                }
            },
            &command_control_);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!discard_result_ && !cancel_requested_.load()) {
                pending_backend_result_ = std::move(result);
                pending_target_ = target;
                pending_action_ = action;
                pending_quick_action_ = quick_action;
                pending_result_ = true;
            } else if (cancel_requested_.load()) {
                status_ = "AI operation stopped.";
                progress_text_ = "The model process was terminated.";
                if (quick_action) {
                    quick_ready_ = true;
                    quick_readiness_status_ = "Stopped — ready to run again";
                }
            }
            busy_ = false;
            operation_state_ = OperationState::Idle;
        }
        RequestRedraw();
    });
    return true;
}

bool AiActionsModalContent::StartQuickAction(
    AiActionType action,
    std::string& error) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (busy_) {
            error = "An AI request is already running.";
            status_ = error;
            return false;
        }
        if (quick_readiness_checking_) {
            error = "AI readiness is still being checked.";
            return false;
        }
        if (!quick_ready_) {
            error = quick_readiness_status_.empty()
                ? "AI is not ready."
                : quick_readiness_status_;
            return false;
        }
    }
    RefreshFromConfig();
    return StartAction(action, true, &error);
}

void AiActionsModalContent::Poll() {
    ApplyPendingResult();
}

void AiActionsModalContent::Stop() {
    bool should_stop = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        should_stop = busy_;
        if (should_stop) {
            cancel_requested_.store(true);
            operation_state_ = OperationState::Stopping;
            status_ = command_control_.IsRunning()
                ? "Stopping model process..."
                : "Cancelling AI operation...";
        }
    }
    if (should_stop) {
        command_control_.RequestStop();
        RequestRedraw();
    }
}

bool AiActionsModalContent::CanStop() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return busy_ && operation_state_ != OperationState::Stopping;
}

void AiActionsModalContent::ApplyPendingResult() {
    AiBackendResult result;
    AiDocumentTarget target;
    AiActionType action = AiActionType::Translate;
    bool quick_action = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!pending_result_) {
            return;
        }
        pending_result_ = false;
        result = std::move(pending_backend_result_);
        target = std::move(pending_target_);
        action = pending_action_;
        quick_action = pending_quick_action_;
        pending_quick_action_ = false;
    }

    auto report_failure = [&](std::string message) {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            status_ = message;
            if (quick_action) {
                quick_readiness_status_ = "Action failed — " + message;
            }
        }
        if (quick_action) {
            if (quick_completion_) {
                quick_completion_(false, message);
            } else if (notify_status_) {
                notify_status_(message);
            }
        }
    };

    if (!result.success) {
        report_failure(result.error.empty() ? "AI request failed." : result.error);
        return;
    }
    if (!apply_target_) {
        report_failure("AI result cannot be applied to the document.");
        return;
    }
    std::string error;
    if (!apply_target_(target, result.text, error)) {
        report_failure(error.empty() ? "AI result was not applied." : error);
        return;
    }
    std::string completed_status;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        progress_text_ = TailUtf8(result.text, 120);
        status_ = ActionLabel(action) + " applied to " +
            (target.range.whole_document ? "the whole document." : "the current paragraph.");
        completed_status = status_;
    }
    if (quick_action) {
        if (quick_completion_) {
            quick_completion_(true, completed_status);
        } else if (notify_status_) {
            notify_status_(completed_status);
        }
    }
}

void AiActionsModalContent::PrepareClose() {
    PersistOptions();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        discard_result_ = true;
        pending_result_ = false;
        pending_quick_action_ = false;
    }
    info_visible_ = false;
    active_panel_ = 0;
    cancel_requested_.store(true);
    command_control_.RequestStop();
}

bool AiActionsModalContent::HandleShortcut(ftxui::Event event) {
    if (info_visible_) {
        return false;
    }
    if (event == ftxui::Event::Character("1")) {
        StartAction(AiActionType::Translate, false);
        return true;
    }
    if (event == ftxui::Event::Character("2")) {
        StartAction(AiActionType::Edit, false);
        return true;
    }
    return false;
}

void AiActionsModalContent::ShowInfo() {
    info_visible_ = true;
    active_panel_ = 1;
    close_info_button_->TakeFocus();
    RequestRedraw();
}

void AiActionsModalContent::CloseInfo() {
    info_visible_ = false;
    active_panel_ = 0;
    source_language_input_->TakeFocus();
    RequestRedraw();
}

void AiActionsModalContent::RequestRedraw() const {
    if (request_redraw_) {
        request_redraw_();
    }
}

std::string AiActionsModalContent::SelectedModelLabel() const {
    if (!config_ || config_->ai_selected_model_key.empty()) {
        return "Not selected";
    }
    return AiBackend::ModelIdFromKey(config_->ai_selected_model_key);
}

std::string AiActionsModalContent::SelectedBackendLabel() const {
    if (!config_) {
        return "Unavailable";
    }
    const std::string& key = config_->ai_selected_model_key;
    if (key.rfind("ollama:", 0) == 0) {
        return AiBackend::ProviderLabel(AiProvider::Ollama);
    }
    if (key.rfind("openai:", 0) == 0) {
        return AiBackend::ProviderLabel(AiProvider::OpenAiCompatible);
    }
    if (key.rfind("local:", 0) == 0) {
        return AiBackend::ProviderLabel(AiProvider::LocalLlamaCpp);
    }
    return AiBackend::ProviderLabel(AiBackend::ProviderFromConfig(config_->ai_provider));
}

ftxui::Element AiActionsModalContent::RenderLanguageSuggestions(const Theme& theme) {
    using namespace ftxui;
    const bool source_active = source_language_input_->Focused() || source_language_menu_->Focused();
    const bool target_active = target_language_input_->Focused() || target_language_menu_->Focused();
    if (!source_active && !target_active) {
        return vbox({
            paragraph("Type to filter, then choose a language from the list. Custom values are not accepted.") |
                dim | color(theme.modal_text_color),
            filler(),
        }) | size(HEIGHT, EQUAL, 5);
    }
    Element source = source_active
        ? source_language_menu_->Render() |
              borderStyled(LIGHT, theme.modal_border) |
              size(HEIGHT, EQUAL, 5)
        : text("") | size(HEIGHT, EQUAL, 5);
    Element target = target_active
        ? target_language_menu_->Render() |
              borderStyled(LIGHT, theme.modal_border) |
              size(HEIGHT, EQUAL, 5)
        : text("") | size(HEIGHT, EQUAL, 5);
    return hbox({source | flex, text("  "), target | flex}) | size(HEIGHT, EQUAL, 5);
}

ftxui::Element AiActionsModalContent::RenderInfoPopup(const Theme& theme) {
    using namespace ftxui;
    return vbox({
        text(" AI Actions help") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        paragraph(
            "Click 1 Translate or press 1 to translate. Click 2 Edit or press 2 to edit. "
            "The default scope is the paragraph containing the cursor. Enable Whole document "
            "to process the entire file. Translation languages must be selected from the filtered lists.") |
            color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        hbox({filler(), close_info_button_->Render()}),
    }) | borderStyled(LIGHT, theme.modal_border) |
        size(WIDTH, LESS_THAN, 68) |
        clear_under;
}

ftxui::Element AiActionsModalContent::Render() {
    ApplyPendingResult();
    FinalizeUnfocusedLanguageInputs();
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ftxui::Element body = RenderContent(theme) |
        ftxui::bgcolor(theme.modal_input_bg) |
        ftxui::color(theme.modal_input_fg);
    if (!info_visible_) {
        return body;
    }
    return ftxui::dbox({
        body,
        ftxui::vbox({
            ftxui::filler(),
            ftxui::hbox({ftxui::filler(), RenderInfoPopup(theme), ftxui::filler()}),
            ftxui::filler(),
        }),
    });
}

ftxui::Element AiActionsModalContent::RenderContent(const Theme& theme) {
    using namespace ftxui;
    bool busy = false;
    std::string status;
    std::string progress;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        busy = busy_;
        status = status_;
        progress = progress_text_;
    }
    if (busy) {
        ++progress_frame_;
    }

    Element status_panel = vbox({
        hbox({
            busy ? spinner(0, progress_frame_) | bold : text(" "),
            text(" Status: ") | bold | color(theme.modal_accent),
            paragraph(status) | color(theme.modal_text_color) | flex,
        }),
        separator() | color(theme.modal_border),
        text(" Progress — latest 120 characters") | bold | color(theme.modal_accent),
        paragraph(progress.empty() ? "No AI output yet." : progress) |
            color(theme.modal_text_color) |
            size(HEIGHT, LESS_THAN, 5),
    }) | borderStyled(LIGHT, theme.modal_border) | size(HEIGHT, EQUAL, 10);

    return vbox({
        hbox({
            LabelValue("Model", SelectedModelLabel(), theme) | flex,
            text("   "),
            LabelValue("Backend", SelectedBackendLabel(), theme),
        }),
        separator() | color(theme.modal_border),
        hbox({
            text(" Translation: ") | bold | color(theme.modal_text_color),
            text(committed_source_language_) | bold | color(theme.modal_accent),
            text("  ->  ") | color(theme.modal_text_color),
            text(committed_target_language_) | bold | color(theme.modal_accent),
        }),
        hbox({
            text(" From: ") | bold | color(theme.modal_accent),
            source_language_input_->Render() |
                borderStyled(LIGHT, theme.modal_border) | flex,
            text("   To: ") | bold | color(theme.modal_accent),
            target_language_input_->Render() |
                borderStyled(LIGHT, theme.modal_border) | flex,
        }),
        RenderLanguageSuggestions(theme),
        separator() | color(theme.modal_border),
        hbox({
            text(" Editing style: ") | bold | color(theme.modal_accent),
            conversational_button_->Render(),
            text(" "),
            business_button_->Render(),
        }),
        hbox({
            text(" Scope: ") | bold | color(theme.modal_accent),
            whole_document_checkbox_->Render(),
        }),
        hbox({
            filler(),
            translate_button_->Render(),
            text("   "),
            edit_button_->Render(),
            filler(),
        }),
        status_panel,
    }) | borderStyled(LIGHT, theme.modal_border);
}

AiActionsModal::AiActionsModal(
    const Theme* theme,
    EditorConfig* config,
    CaptureAiTargetCallback capture_target,
    ApplyAiTargetCallback apply_target,
    std::function<void()> request_redraw,
    std::function<void(const std::string&)> notify_status,
    std::function<void(bool, const std::string&)> quick_completion)
    : theme_(theme) {
    content_ = std::make_shared<AiActionsModalContent>(
        theme_,
        config,
        std::move(capture_target),
        std::move(apply_target),
        std::move(request_redraw),
        std::move(notify_status),
        std::move(quick_completion));
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterButtons({
        {"Info", [this] { content_->ShowInfo(); }, ButtonRole::Default},
        {"Stop", [this] { content_->Stop(); }, ButtonRole::Warning,
         [this] { return content_->CanStop(); }},
        {"Close", [this] { Close(); }, ButtonRole::Cancel},
    });
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component AiActionsModal::View() const {
    return modal_;
}

void AiActionsModal::Open() {
    open_ = true;
    content_->SetTheme(theme_);
    content_->RefreshFromConfig();
    modal_->SetTheme(theme_);
    content_->GetMainComponent()->TakeFocus();
}

void AiActionsModal::Close() {
    if (content_) {
        content_->PrepareClose();
    }
    open_ = false;
}

bool AiActionsModal::IsOpen() const {
    return open_;
}

bool AiActionsModal::OnEvent(ftxui::Event event) {
    if (!open_ || !modal_) {
        return false;
    }
    if (content_ && content_->HandleShortcut(event)) {
        return true;
    }
    return modal_->OnEvent(std::move(event));
}

bool AiActionsModal::StartQuickAction(AiActionType action, std::string& error) {
    return content_ && content_->StartQuickAction(action, error);
}

void AiActionsModal::PrepareQuickActions() {
    if (content_) {
        content_->PrepareQuickActions();
    }
}

AiQuickStatusSnapshot AiActionsModal::QuickStatus() const {
    return content_ ? content_->QuickStatus() : AiQuickStatusSnapshot{};
}

void AiActionsModal::StopQuickAction() {
    if (content_) {
        content_->StopQuickAction();
    }
}

void AiActionsModal::Poll() {
    if (content_) {
        content_->Poll();
    }
}

} // namespace textlt
