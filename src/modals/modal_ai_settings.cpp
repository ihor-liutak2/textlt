#include "modals/modal_ai_settings.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ui_button.hpp"

namespace textlt {
namespace {

std::string FormatAiSeconds(double milliseconds) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << milliseconds / 1000.0;
    return stream.str();
}

std::string TestCompletionStatus(const AiBackendResult& result) {
    std::string status = "AI editing test completed. Finish: " +
        AiBackend::FinishReasonLabel(result.finish_reason);
    if (result.generated_tokens > 0) {
        status += " · " + std::to_string(result.generated_tokens) + " tokens";
    }
    if (result.tokens_per_second > 0.0) {
        std::ostringstream speed;
        speed << std::fixed << std::setprecision(1) << result.tokens_per_second;
        status += " · " + speed.str() + " tok/s";
    }
    if (result.time_to_first_token_ms > 0.0) {
        status += " · first token " + FormatAiSeconds(result.time_to_first_token_ms) + " s";
    }
    return status + ".";
}

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

} // namespace

AiSettingsModalContent::AiSettingsModalContent(
    const Theme* theme,
    EditorConfig* config,
    std::function<void()> request_redraw)
    : theme_(theme),
      config_(config),
      request_redraw_(std::move(request_redraw)) {
    ftxui::InputOption input_option;
    input_option.multiline = false;
    input_option.cursor_position = &server_url_cursor_;
    input_option.on_enter = [this] { ConnectAndRefreshServerModels(); };
    input_option.transform = [this](ftxui::InputState state) {
        const Theme& resolved = theme_ ? *theme_ : FallbackTheme();
        return resolved.InputTransform(std::move(state));
    };
    server_url_input_ = ftxui::Input(
        &server_url_, "http://127.0.0.1:11434", input_option);

    provider_ollama_button_ = MakeButton(
        &theme_, "Ollama", [this] { SelectProvider(AiProvider::Ollama); },
        ButtonRole::Toggle,
        [this] { return provider_ == AiProvider::Ollama; });
    provider_openai_button_ = MakeButton(
        &theme_, "OpenAI-compatible",
        [this] { SelectProvider(AiProvider::OpenAiCompatible); },
        ButtonRole::Toggle,
        [this] { return provider_ == AiProvider::OpenAiCompatible; });
    test_button_ = MakeButton(
        &theme_, "Test model", [this] { StartTest(); }, ButtonRole::Primary);
    close_test_button_ = MakeButton(
        &theme_, "Close", [this] { CloseTestPopup(); }, ButtonRole::Secondary);

    ftxui::MenuOption menu_option = ftxui::MenuOption::Vertical();
    menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& resolved = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(
            std::string(state.active ? "[x] " : "[ ] ") + state.label);
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(resolved.modal_selected_item_bg) |
                ftxui::color(resolved.modal_selected_item_fg);
        }
        return row | ftxui::color(resolved.modal_text_color);
    };
    model_menu_ = ftxui::Menu(&model_labels_, &selected_model_, menu_option);

    auto main_content = ftxui::Container::Vertical({
        server_url_input_,
        ftxui::Container::Horizontal({
            provider_ollama_button_, provider_openai_button_,
        }),
        model_menu_,
        test_button_,
    });
    auto test_content = ftxui::Container::Vertical({close_test_button_});
    auto panels = ftxui::Container::Tab({main_content, test_content}, &active_panel_);
    container_ = ftxui::CatchEvent(panels, [this](ftxui::Event event) {
        bool test_popup_visible = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            test_popup_visible = test_popup_visible_;
        }
        if (event == ftxui::Event::Escape && test_popup_visible) {
            CloseTestPopup();
            return true;
        }
        return false;
    });
    RefreshFromConfig();
}

AiSettingsModalContent::~AiSettingsModalContent() {
    cancel_requested_.store(true);
    command_control_.RequestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void AiSettingsModalContent::RefreshFromConfig() {
    if (config_) {
        server_url_ = config_->ai_server_url;
        provider_ = AiBackend::ProviderFromConfig(config_->ai_provider);
        if (provider_ == AiProvider::Auto) {
            provider_ = AiProvider::Ollama;
        }
    }
    server_url_cursor_ = static_cast<int>(server_url_.size());
    models_.clear();
    LoadModels();
}

void AiSettingsModalContent::LoadModels() {
    const std::string selected_key = config_ ? config_->ai_selected_model_key : std::string{};
    model_labels_.clear();
    model_descriptions_.clear();

    for (const AiModelInfo& model : models_) {
        model_labels_.push_back(model.title + " | " + model.provider_label);
        model_descriptions_.push_back(model.description.empty()
            ? "Model reported by the configured AI server."
            : model.description);
    }

    if (!selected_key.empty() &&
        std::none_of(models_.begin(), models_.end(), [&](const AiModelInfo& model) {
            return model.key == selected_key;
        })) {
        const bool ollama = selected_key.rfind("ollama:", 0) == 0;
        const bool openai = selected_key.rfind("openai:", 0) == 0;
        if ((ollama && provider_ == AiProvider::Ollama) ||
            (openai && provider_ == AiProvider::OpenAiCompatible)) {
            AiModelInfo placeholder;
            placeholder.key = selected_key;
            placeholder.id = AiBackend::ModelIdFromKey(selected_key);
            placeholder.title = placeholder.id;
            placeholder.provider_label = ollama ? "Ollama" : "OpenAI-compatible";
            placeholder.description =
                "Previously selected server model. Connect to refresh its availability.";
            models_.push_back(placeholder);
            model_labels_.push_back(placeholder.title + " | " + placeholder.provider_label);
            model_descriptions_.push_back(placeholder.description);
        }
    }

    if (model_labels_.empty()) {
        model_labels_.push_back("No models. Connect to a running AI server.");
        selected_model_ = 0;
        persisted_model_index_ = -1;
        return;
    }

    selected_model_ = 0;
    persisted_model_index_ = -1;
    for (size_t index = 0; index < models_.size(); ++index) {
        if (models_[index].key == selected_key) {
            selected_model_ = static_cast<int>(index);
            persisted_model_index_ = selected_model_;
            break;
        }
    }
}

void AiSettingsModalContent::StartWorker(std::function<void()> operation) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (busy_) {
            status_ = "Another AI settings operation is already running.";
            return;
        }
        busy_ = true;
        operation_state_ = OperationState::Starting;
        cancel_requested_.store(false);
        command_control_.Reset();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    RequestRedraw();
    worker_ = std::thread([this, operation = std::move(operation)] {
        operation();
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            busy_ = false;
            operation_state_ = OperationState::Idle;
        }
        RequestRedraw();
    });
}

bool AiSettingsModalContent::SaveConnectionSettings() {
    server_url_ = AiBackend::NormalizeServerUrl(server_url_);
    if (server_url_.empty()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "AI server URL cannot be empty.";
        return false;
    }
    if (!AiBackend::IsSupportedServerUrl(server_url_)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "AI server URL must start with http:// or https://.";
        return false;
    }
    if (config_) {
        const bool changed = config_->ai_server_url != server_url_ ||
            config_->ai_provider != AiBackend::ProviderToConfig(provider_);
        config_->ai_server_url = server_url_;
        config_->ai_provider = AiBackend::ProviderToConfig(provider_);
        if (!config_->Persist()) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            status_ = "Could not save AI settings.";
            return false;
        }
        if (changed) {
            connection_status_ = "Not checked after settings change";
        }
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    status_ = "AI connection settings saved.";
    return true;
}

void AiSettingsModalContent::SelectProvider(AiProvider provider) {
    provider_ = provider;
    models_.clear();
    if (config_) {
        config_->ai_selected_model_key.clear();
    }
    LoadModels();
    std::lock_guard<std::mutex> lock(state_mutex_);
    connection_status_ = "Not checked after provider change";
    status_ = "Provider selected. Connect to load models from its URL.";
}

void AiSettingsModalContent::ConnectAndRefreshServerModels() {
    if (!SaveConnectionSettings()) {
        return;
    }
    AiBackendSettings settings;
    settings.server_url = server_url_;
    settings.provider = provider_;
    settings.selected_model_key = config_ ? config_->ai_selected_model_key : std::string{};
    settings.timeout_seconds = 30;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "Connecting to AI server...";
        connection_status_ = "Connecting to " + AiBackend::ProviderLabel(provider_);
    }
    StartWorker([this, settings] {
        const AiConnectionResult result =
            AiBackend(settings).CheckConnectionAndListModels(
                &cancel_requested_, &command_control_);
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_models_ready_ = true;
        pending_connection_success_ = result.success;
        pending_detected_provider_ = result.provider;
        pending_models_ = result.models;
        pending_status_ = cancel_requested_.load()
            ? "Connection check stopped."
            : (result.success
                ? result.provider_label + " connected. " +
                    std::to_string(result.models.size()) + " model(s) found."
                : result.error);
    });
}

void AiSettingsModalContent::PersistSelectedModel() {
    if (!config_ || selected_model_ == persisted_model_index_ || selected_model_ < 0 ||
        static_cast<size_t>(selected_model_) >= models_.size()) {
        return;
    }
    persisted_model_index_ = selected_model_;
    const AiModelInfo& selected = models_[static_cast<size_t>(selected_model_)];
    config_->ai_selected_model_key = selected.key;
    const bool saved = config_->Persist();
    std::lock_guard<std::mutex> lock(state_mutex_);
    status_ = saved
        ? "Selected model: " + selected.title
        : "Model selected, but settings could not be saved.";
}

void AiSettingsModalContent::StartTest() {
    PersistSelectedModel();
    if (!config_ || config_->ai_selected_model_key.empty()) {
        test_popup_visible_ = true;
        test_result_ = "Connect to a server and select a model before running the test.";
        status_ = "AI test cannot start without a selected server model.";
        active_panel_ = 1;
        close_test_button_->TakeFocus();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (busy_) {
            test_popup_visible_ = true;
            test_result_ = "Another AI settings operation is already running.";
            status_ = test_result_;
            active_panel_ = 1;
            close_test_button_->TakeFocus();
            return;
        }
        test_popup_visible_ = true;
        test_running_ = true;
        test_result_ = "Waiting for server output...";
        status_ = "Running editing test with the selected server model...";
    }
    active_panel_ = 1;
    close_test_button_->TakeFocus();

    AiPromptRequest request;
    request.action = AiActionType::Edit;
    request.text = test_source_;
    request.edit_style = AiEditStyle::Conversational;
    AiBackendSettings settings;
    settings.server_url = config_->ai_server_url;
    settings.provider = AiBackend::ProviderFromConfig(config_->ai_provider);
    settings.selected_model_key = config_->ai_selected_model_key;
    settings.timeout_seconds = 60;
    settings.max_output_tokens = 128;
    StartWorker([this, settings, request] {
        const AiBackendResult result = AiBackend(settings).Run(
            request,
            &cancel_requested_,
            [this](const std::string& generated) {
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    if (operation_state_ == OperationState::Stopping ||
                        cancel_requested_.load() || !test_popup_visible_) {
                        return;
                    }
                    operation_state_ = OperationState::Running;
                    test_result_ = generated;
                }
                RequestRedraw();
            },
            &command_control_);
        std::lock_guard<std::mutex> lock(state_mutex_);
        test_running_ = false;
        if (cancel_requested_.load()) {
            test_result_ = "Test stopped.";
            pending_status_ = "AI test stopped.";
        } else if (!result.success) {
            test_result_ = result.error.empty() ? "AI test failed." : result.error;
            pending_status_ = "AI editing test failed.";
        } else {
            test_result_ = result.text;
            pending_status_ = TestCompletionStatus(result);
        }
    });
}

void AiSettingsModalContent::CloseTestPopup() {
    const bool stop = CanCancelTask();
    if (stop) {
        CancelCurrentTask();
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        test_popup_visible_ = false;
    }
    active_panel_ = 0;
    model_menu_->TakeFocus();
    RequestRedraw();
}

void AiSettingsModalContent::CancelCurrentTask() {
    bool should_cancel = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        should_cancel = busy_ && operation_state_ != OperationState::Stopping;
        if (should_cancel) {
            cancel_requested_.store(true);
            operation_state_ = OperationState::Stopping;
            status_ = "Stopping the current AI request...";
            if (test_running_) {
                test_result_ = "Stopping AI request...";
            }
        }
    }
    if (should_cancel) {
        command_control_.RequestStop();
        RequestRedraw();
    }
}

void AiSettingsModalContent::PrepareClose() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    test_popup_visible_ = false;
    active_panel_ = 0;
}

bool AiSettingsModalContent::CanCancelTask() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return busy_ && operation_state_ != OperationState::Stopping;
}

void AiSettingsModalContent::ApplyPendingWorkerResult() {
    bool models_ready = false;
    bool connection_success = false;
    AiProvider detected = AiProvider::Auto;
    std::string pending_status;
    std::vector<AiModelInfo> pending_models;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        models_ready = pending_models_ready_;
        pending_models_ready_ = false;
        connection_success = pending_connection_success_;
        detected = pending_detected_provider_;
        pending_status = std::move(pending_status_);
        pending_status_.clear();
        if (models_ready) {
            pending_models = std::move(pending_models_);
            pending_models_.clear();
        }
        if (!pending_status.empty()) {
            status_ = pending_status;
        }
    }
    if (models_ready) {
        models_ = connection_success ? std::move(pending_models) : std::vector<AiModelInfo>{};
        connection_status_ = connection_success
            ? "Connected — " + AiBackend::ProviderLabel(detected)
            : (cancel_requested_.load()
                ? "Connection check stopped"
                : AiBackend::ProviderLabel(provider_) + " connection failed");
        LoadModels();
    }
}

void AiSettingsModalContent::RequestRedraw() const {
    if (request_redraw_) {
        request_redraw_();
    }
}

ftxui::Element AiSettingsModalContent::RenderTitle() {
    return ftxui::text(GetTitle());
}

ftxui::Element AiSettingsModalContent::Render() {
    ApplyPendingWorkerResult();
    PersistSelectedModel();
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ftxui::Element body = RenderContent(theme) |
        ftxui::bgcolor(theme.modal_input_bg) |
        ftxui::color(theme.modal_input_fg);
    bool show_test = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        show_test = test_popup_visible_;
    }
    if (!show_test) {
        return body;
    }
    return ftxui::dbox({
        body,
        ftxui::vbox({
            ftxui::filler(),
            ftxui::hbox({ftxui::filler(), RenderTestPopup(theme), ftxui::filler()}),
            ftxui::filler(),
        }),
    });
}

ftxui::Element AiSettingsModalContent::RenderTestPopup(const Theme& theme) {
    using namespace ftxui;
    bool running = false;
    std::string result;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        running = test_running_;
        result = test_result_;
    }
    if (running) {
        ++test_progress_frame_;
    }
    return vbox({
        hbox({
            text(" Server model test") | bold | color(theme.modal_accent),
            filler(),
            running ? spinner(0, test_progress_frame_) | bold : text(""),
        }),
        separator() | color(theme.modal_border),
        paragraph(test_source_) | color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        text(" Result") | bold | color(theme.modal_accent),
        paragraph(result.empty() ? "Waiting for server output..." : result) |
            color(theme.modal_text_color) |
            size(HEIGHT, EQUAL, 9) |
            yframe |
            vscroll_indicator,
        separator() | color(theme.modal_border),
        hbox({filler(), close_test_button_->Render()}),
    }) | borderStyled(LIGHT, theme.modal_border) |
        size(WIDTH, EQUAL, 82) |
        clear_under;
}

ftxui::Element AiSettingsModalContent::RenderContent(const Theme& theme) {
    using namespace ftxui;
    bool busy = false;
    OperationState operation_state = OperationState::Idle;
    std::string status;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        busy = busy_;
        operation_state = operation_state_;
        status = status_;
    }
    if (busy) {
        ++operation_progress_frame_;
    }

    std::string model_description = "Connect to a running server to load its models.";
    if (selected_model_ >= 0 &&
        static_cast<size_t>(selected_model_) < model_descriptions_.size()) {
        model_description = model_descriptions_[static_cast<size_t>(selected_model_)];
    }

    return vbox({
        text(" Connection") | bold | color(theme.modal_text_color),
        hbox({
            text(" URL: ") | bold | color(theme.modal_accent),
            server_url_input_->Render() | borderStyled(LIGHT, theme.modal_border) | flex,
        }),
        hbox({
            text(" API: ") | bold | color(theme.modal_accent),
            provider_ollama_button_->Render(), text(" "),
            provider_openai_button_->Render(),
        }),
        paragraph(
            "TextLT only connects to an already-running AI service. "
            "Models and server processes are managed outside TextLT.") |
            dim | color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        hbox({
            text(" Server models") | bold | color(theme.modal_text_color),
            filler(),
            test_button_->Render(),
        }),
        model_menu_->Render() |
            borderStyled(LIGHT, theme.modal_border) |
            size(HEIGHT, EQUAL, 7),
        paragraph(" " + model_description) |
            dim | color(theme.modal_text_color) | size(HEIGHT, EQUAL, 2),
        text(" [x] active model · model management belongs to the configured server") |
            dim | color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        vbox({
            StatusLine("Connection", connection_status_, theme),
            StatusLine("Task", busy
                ? (operation_state == OperationState::Stopping ? "Stopping" : "Running")
                : "Idle", theme),
            hbox({
                busy ? spinner(0, operation_progress_frame_) | bold : text(" "),
                text(" Status: ") | bold | color(theme.modal_accent),
                paragraph(status.empty() ? "Ready" : status) |
                    color(theme.modal_text_color) | flex,
            }),
        }) | borderStyled(LIGHT, theme.modal_border) | size(HEIGHT, EQUAL, 6),
    }) | borderStyled(LIGHT, theme.modal_border);
}

AiSettingsModal::AiSettingsModal(
    const Theme* theme,
    EditorConfig* config,
    std::function<void()> request_redraw)
    : theme_(theme) {
    content_ = std::make_shared<AiSettingsModalContent>(
        theme_, config, std::move(request_redraw));
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterButtons({
        {"Save", [this] { content_->SaveConnectionSettings(); }, ButtonRole::Default},
        {"Connect / Refresh", [this] { content_->ConnectAndRefreshServerModels(); }, ButtonRole::Primary},
        {"Cancel Task", [this] { content_->CancelCurrentTask(); }, ButtonRole::Warning,
         [this] { return content_->CanCancelTask(); }},
        {"Close", [this] { Close(); }, ButtonRole::Cancel},
    });
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component AiSettingsModal::View() const {
    return modal_;
}

void AiSettingsModal::Open() {
    open_ = true;
    content_->SetTheme(theme_);
    content_->RefreshFromConfig();
    modal_->SetTheme(theme_);
    content_->GetMainComponent()->TakeFocus();
}

void AiSettingsModal::Close() {
    if (content_) {
        content_->PrepareClose();
    }
    open_ = false;
}

bool AiSettingsModal::IsOpen() const {
    return open_;
}

bool AiSettingsModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}

} // namespace textlt
