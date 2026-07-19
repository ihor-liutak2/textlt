#include "modals/modal_ai.hpp"

#include <functional>
#include <string>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ui_button.hpp"

namespace textlt {
namespace {

ftxui::Element StatusLine(
    const std::string& label,
    const std::string& value,
    const Theme& theme) {
    using namespace ftxui;
    return hbox({
        text(" " + label + ": ") | bold | color(theme.modal_accent),
        paragraph(value.empty() ? "-" : value) | color(theme.modal_text_color) | flex,
    });
}

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

} // namespace

AiActionsModalContent::AiActionsModalContent(
    const Theme* theme,
    EditorConfig* config,
    CaptureAiTargetCallback capture_target,
    ApplyAiTargetCallback apply_target,
    std::function<void()> request_redraw)
    : theme_(theme),
      config_(config),
      capture_target_(std::move(capture_target)),
      apply_target_(std::move(apply_target)),
      request_redraw_(std::move(request_redraw)) {
    ftxui::InputOption language_option;
    language_option.multiline = false;
    language_option.cursor_position = &language_cursor_;
    language_option.on_enter = [this] { StartAction(AiActionType::Translate); };
    language_option.transform = [this](ftxui::InputState state) {
        const Theme& resolved = theme_ ? *theme_ : FallbackTheme();
        return resolved.InputTransform(std::move(state));
    };
    language_input_ = ftxui::Input(&language_, "Ukrainian", language_option);

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
            return item |
                ftxui::bgcolor(resolved.modal_selected_item_bg) |
                ftxui::color(resolved.modal_selected_item_fg);
        }
        return item | ftxui::color(resolved.modal_text_color);
    };
    whole_document_checkbox_ = ftxui::Checkbox(
        "Whole document (otherwise current paragraph)",
        &whole_document_,
        checkbox_option);

    translate_button_ = MakeButton(
        &theme_, "1 Translate", [this] { StartAction(AiActionType::Translate); },
        ButtonRole::Primary);
    edit_button_ = MakeButton(
        &theme_, "2 Edit", [this] { StartAction(AiActionType::Edit); },
        ButtonRole::Primary);

    container_ = ftxui::CatchEvent(
        ftxui::Container::Vertical({
            language_input_,
            ftxui::Container::Horizontal({conversational_button_, business_button_}),
            whole_document_checkbox_,
            ftxui::Container::Horizontal({translate_button_, edit_button_}),
        }),
        [this](ftxui::Event event) { return HandleShortcut(std::move(event)); });
    RefreshFromConfig();
}

AiActionsModalContent::~AiActionsModalContent() {
    PersistOptions();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void AiActionsModalContent::RefreshFromConfig() {
    if (!config_) {
        return;
    }
    language_ = config_->ai_translation_language.empty()
        ? std::string("Ukrainian")
        : config_->ai_translation_language;
    language_cursor_ = static_cast<int>(language_.size());
    edit_style_ = config_->ai_edit_style == "business"
        ? AiEditStyle::Business
        : AiEditStyle::Conversational;
    whole_document_ = config_->ai_whole_document;
    persisted_language_ = language_;
    persisted_edit_style_ = edit_style_;
    persisted_whole_document_ = whole_document_;
}

void AiActionsModalContent::SetEditStyle(AiEditStyle style) {
    edit_style_ = style;
    PersistOptions();
}

void AiActionsModalContent::PersistOptions() {
    if (!config_) {
        return;
    }
    if (language_.empty()) {
        language_ = "Ukrainian";
        language_cursor_ = static_cast<int>(language_.size());
    }
    if (persisted_language_ == language_ && persisted_edit_style_ == edit_style_ &&
        persisted_whole_document_ == whole_document_) {
        return;
    }
    config_->ai_translation_language = language_;
    config_->ai_edit_style = edit_style_ == AiEditStyle::Business
        ? "business"
        : "conversational";
    config_->ai_whole_document = whole_document_;
    const bool saved = config_->Persist();
    persisted_language_ = language_;
    persisted_edit_style_ = edit_style_;
    persisted_whole_document_ = whole_document_;
    if (!saved) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "Could not save AI action settings.";
    }
}

void AiActionsModalContent::StartAction(AiActionType action) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (busy_) {
            status_ = "An AI request is already running.";
            return;
        }
    }
    PersistOptions();
    if (!config_ || config_->ai_selected_model_key.empty()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "Select an AI model in AI Settings first.";
        return;
    }
    if (!capture_target_) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "AI text capture is unavailable.";
        return;
    }

    AiDocumentTarget target;
    std::string error;
    if (!capture_target_(whole_document_, target, error)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = error.empty() ? "Could not read the target text." : error;
        return;
    }

    AiPromptRequest request;
    request.action = action;
    request.text = target.range.original_text;
    request.target_language = language_;
    request.edit_style = edit_style_;
    const AiBackendSettings settings{
        config_->ai_server_url,
        AiBackend::ProviderFromConfig(config_->ai_provider),
        config_->ai_selected_model_key,
        180,
    };

    if (worker_.joinable()) {
        worker_.join();
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        busy_ = true;
        discard_result_ = false;
        status_ = ActionLabel(action) + " is running...";
    }
    RequestRedraw();
    worker_ = std::thread([this, settings, request, target = std::move(target), action] {
        AiBackendResult result = AiBackend(settings).Run(request);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!discard_result_) {
                pending_backend_result_ = std::move(result);
                pending_target_ = target;
                pending_action_ = action;
                pending_result_ = true;
            } else {
                status_ = "AI result discarded because the modal was closed.";
            }
            busy_ = false;
        }
        RequestRedraw();
    });
}

void AiActionsModalContent::ApplyPendingResult() {
    AiBackendResult result;
    AiDocumentTarget target;
    AiActionType action = AiActionType::Translate;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!pending_result_) {
            return;
        }
        pending_result_ = false;
        result = std::move(pending_backend_result_);
        target = std::move(pending_target_);
        action = pending_action_;
    }

    if (!result.success) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = result.error.empty() ? "AI request failed." : result.error;
        return;
    }
    if (!apply_target_) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "AI result cannot be applied to the document.";
        return;
    }
    std::string error;
    if (!apply_target_(target, result.text, error)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = error.empty() ? "AI result was not applied." : error;
        return;
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    status_ = ActionLabel(action) + " applied to " +
        (target.range.whole_document ? "the whole document." : "the current paragraph.");
}


void AiActionsModalContent::PrepareClose() {
    PersistOptions();
    std::lock_guard<std::mutex> lock(state_mutex_);
    discard_result_ = true;
    pending_result_ = false;
    if (busy_) {
        status_ = "AI request continues in the background; its result will be discarded.";
    }
}

bool AiActionsModalContent::HandleShortcut(ftxui::Event event) {
    if (event == ftxui::Event::Character("1")) {
        StartAction(AiActionType::Translate);
        return true;
    }
    if (event == ftxui::Event::Character("2")) {
        StartAction(AiActionType::Edit);
        return true;
    }
    return false;
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

std::string AiActionsModalContent::GetFooterText() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return status_.size() <= 68 ? status_ : status_.substr(0, 68);
}

ftxui::Element AiActionsModalContent::Render() {
    ApplyPendingResult();
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return RenderContent(theme) |
        ftxui::bgcolor(theme.modal_input_bg) |
        ftxui::color(theme.modal_input_fg);
}

ftxui::Element AiActionsModalContent::RenderContent(const Theme& theme) {
    using namespace ftxui;
    bool busy = false;
    std::string status;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        busy = busy_;
        status = status_;
    }

    return vbox({
        StatusLine("Model", SelectedModelLabel(), theme),
        StatusLine("Backend", SelectedBackendLabel(), theme),
        separator() | color(theme.modal_border),
        hbox({
            text(" Translation language: ") | bold | color(theme.modal_accent),
            language_input_->Render() | flex,
        }),
        hbox({
            text(" Editing style: ") | bold | color(theme.modal_accent),
            conversational_button_->Render(),
            text(" "),
            business_button_->Render(),
        }),
        whole_document_checkbox_->Render(),
        separator() | color(theme.modal_border),
        hbox({
            filler(),
            translate_button_->Render(),
            text("   "),
            edit_button_->Render(),
            filler(),
        }),
        text(" Mouse: click an action. Keyboard: 1 = Translate, 2 = Edit.") |
            dim | color(theme.modal_text_color),
        text(" Default scope is the paragraph containing the cursor.") |
            dim | color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        StatusLine("Status", busy ? status + " Please wait..." : status, theme),
    }) | borderStyled(LIGHT, theme.modal_border);
}

AiActionsModal::AiActionsModal(
    const Theme* theme,
    EditorConfig* config,
    CaptureAiTargetCallback capture_target,
    ApplyAiTargetCallback apply_target,
    std::function<void()> request_redraw)
    : theme_(theme) {
    content_ = std::make_shared<AiActionsModalContent>(
        theme_,
        config,
        std::move(capture_target),
        std::move(apply_target),
        std::move(request_redraw));
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterButtons({{"Close", [this] { Close(); }}});
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

} // namespace textlt
