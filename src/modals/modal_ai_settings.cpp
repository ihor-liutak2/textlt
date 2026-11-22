#include "modals/modal_ai_settings.hpp"

#include "ai/ai_active_settings.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "ai/ai_backend.hpp"
#include "ftxui/component/component_options.hpp"
#include "remote/remote_command_runner.hpp"
#include "shortcut_key.hpp"
#include "ui_button.hpp"
#include "ui_selectable_input.hpp"

namespace textlt {
namespace {

using namespace ftxui;

const Theme& DefaultModalTheme() {
    static const Theme theme = FallbackTheme();
    return theme;
}

std::string Trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} :
        value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

bool NeedsKey(AiProvider provider) {
    return provider == AiProvider::OpenCodeGo || provider == AiProvider::Claude ||
        provider == AiProvider::Mimo;
}

struct ProviderForm {
    AiProvider provider;
    AiProviderConfig draft;
    std::string effort;
    std::vector<AiModelInfo> models;
    std::vector<std::string> model_labels;
    int selected_model = 0;
    size_t revision = 0;
    std::optional<AiModelInfo> inspected_model;
    int endpoint_cursor = 0;
    int key_cursor = 0;
    int model_cursor = 0;
    int effort_cursor = 0;
    bool key_masked = true;
    std::string key_visibility_label = "Show";
    Component endpoint_input;
    Component key_input;
    Component key_visibility_button;
    Component go_checkbox;
    Component model_input;
    Component effort_input;
    Component models_menu;
    Component actions;

    explicit ProviderForm(AiProvider type) : provider(type) {}

    AiBackendSettings Settings() const {
        AiBackendSettings settings;
        settings.provider = provider;
        settings.server_url = Trim(draft.endpoint);
        settings.api_key = Trim(draft.api_key);
        settings.model = Trim(draft.model);
        settings.effort = Trim(effort);
        settings.opencode_use_go = draft.opencode_use_go;
        return settings;
    }
};

} // namespace

class AiSettingsModalContent final : public IModalContent {
public:
    AiSettingsModalContent(const Theme* theme, EditorConfig* config,
        std::function<void()> redraw, std::function<void()> close,
        std::function<std::string()> read_clipboard,
        std::function<bool(const std::string&)> write_clipboard);
    ~AiSettingsModalContent() override;

    std::string GetTitle() override { return "AI Settings"; }
    ModalSizePreference GetModalSizePreference() const override { return {112, 31}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    bool HasCustomFooter() const override { return true; }
    int GetCustomFooterHeight() const override { return 4; }
    Component GetMainComponent() override { return container_; }
    Element Render() override;
    Element RenderCustomFooter() override;

    void Open();
    void Close();
    void Cancel();

private:
    enum class Task { Models, Efforts, Test };

    struct TaskResult {
        size_t provider_index;
        size_t revision;
        Task task;
        std::string message;
        bool success = false;
        std::vector<AiModelInfo> models;
    };
    struct FooterMessage {
        std::string text;
        std::string copy_status;
        bool error = false;
    };

    Component MakeButton(std::string label, std::function<void()> click,
        ButtonRole role = ButtonRole::Default, int tab = -1);
    MenuOption ConnectionMenu() const;
    Component MakeInput(std::string& value, int& cursor, std::string placeholder,
        bool* password = nullptr, std::function<void()> change = {});
    Component MakeKeyVisibilityButton(ProviderForm& form);
    Component MakeGoCheckbox(ProviderForm& form);
    Component FocusedInput() const;
    bool HandlePaste(const Event& event);
    void PasteInto(const Component& input, std::string text);
    void ResetInputState();
    Element RenderOverview();
    Element RenderProvider(const ProviderForm& form);
    Element RenderInfo();
    Component CurrentActions() const;
    void ShowInfo();
    void CloseInfo();
    void CopyMessage();
    void SelectTab(int tab);
    void RefreshOverview();
    void Save(size_t index, bool activate, bool saved_only = false);
    void StartTask(size_t index, Task task);
    void InvalidateModels(ProviderForm& form);
    void ApplyTask();
    void SetMessage(std::string message, bool error = false, int tab = -1);
    void LoadEffort(ProviderForm& form);
    void UseModel(size_t index);

    const Theme& theme_;
    EditorConfig* config_;
    std::function<void()> redraw_;
    std::function<std::string()> read_clipboard_;
    std::function<bool(const std::string&)> write_clipboard_;
    std::vector<ProviderForm> forms_;
    std::vector<std::string> overview_labels_;
    int selected_provider_ = 0;
    int tab_ = 0;
    std::vector<FooterMessage> messages_;
    int info_panel_ = 0;
    bool busy_ = false;
    std::thread worker_;
    std::mutex result_mutex_;
    std::optional<TaskResult> pending_;
    std::atomic<bool> cancelled_{false};
    RemoteCommandControl command_;

    std::vector<std::shared_ptr<InputSelection>> input_selections_;
    bool terminal_paste_active_ = false;
    Component terminal_paste_target_;
    std::string terminal_paste_text_;

    Component tabs_;
    Component overview_;
    Component overview_actions_;
    Component copy_message_;
    Component close_info_;
    Component stop_;
    Component close_;
    Component container_;
};

AiSettingsModalContent::AiSettingsModalContent(
    const Theme* theme, EditorConfig* config,
    std::function<void()> redraw, std::function<void()> close,
    std::function<std::string()> read_clipboard,
    std::function<bool(const std::string&)> write_clipboard)
    : theme_(theme ? *theme : DefaultModalTheme()), config_(config),
      redraw_(std::move(redraw)), read_clipboard_(std::move(read_clipboard)),
      write_clipboard_(std::move(write_clipboard)) {
    for (const auto provider : {AiProvider::Ollama, AiProvider::OpenCodeGo, AiProvider::ChatGpt,
            AiProvider::Claude, AiProvider::Mimo}) {
        forms_.emplace_back(provider);
    }
    if (config_ && config_->ai_providers.count("openai") != 0) {
        forms_.emplace_back(AiProvider::OpenAiCompatible);
    }
    messages_.resize(forms_.size() + 1);

    tabs_ = Container::Horizontal({});
    tabs_->Add(MakeButton("Providers", [this] { SelectTab(0); }, ButtonRole::Toggle, 0));
    overview_ = Menu(&overview_labels_, &selected_provider_, ConnectionMenu());
    overview_actions_ = Container::Horizontal({
        MakeButton("Info", [this] { ShowInfo(); }, ButtonRole::Utility),
        MakeButton("Configure", [this] { SelectTab(selected_provider_ + 1); }),
        MakeButton("Set Active", [this] { Save(selected_provider_, true, true); }, ButtonRole::Primary),
    });
    auto panels = Container::Tab({
        Container::Vertical({overview_, overview_actions_}),
    }, &tab_);
    for (size_t index = 0; index < forms_.size(); ++index) {
        auto& form = forms_[index];
        const int tab = static_cast<int>(index + 1);
        const auto label = AiBackend::ProviderLabel(form.provider);
        tabs_->Add(MakeButton(label,
            [this, tab] { SelectTab(tab); }, ButtonRole::Toggle, tab));

        const auto invalidate = [this, index] { InvalidateModels(forms_[index]); };
        form.endpoint_input = MakeInput(form.draft.endpoint, form.endpoint_cursor,
            AiBackend::DefaultEndpoint(form.provider), nullptr, invalidate);
        if (NeedsKey(form.provider)) {
            form.key_input = MakeInput(
                form.draft.api_key, form.key_cursor, "Paste API key", &form.key_masked, invalidate);
            form.key_visibility_button = MakeKeyVisibilityButton(form);
        }
        if (form.provider == AiProvider::OpenCodeGo) {
            form.go_checkbox = MakeGoCheckbox(form);
        }
        form.model_input = MakeInput(form.draft.model, form.model_cursor,
            "Choose from list or enter model ID", nullptr,
            [this, index] { LoadEffort(forms_[index]); });
        if (form.provider != AiProvider::OpenAiCompatible) {
            form.effort_input = MakeInput(form.effort, form.effort_cursor, "Provider default");
        }
        form.models_menu = Menu(&form.model_labels, &form.selected_model, ConnectionMenu());
        form.actions = Container::Horizontal({
            MakeButton("Info", [this] { ShowInfo(); }, ButtonRole::Utility),
            MakeButton("Get Models", [this, index] { StartTask(index, Task::Models); }),
            MakeButton("Use Model", [this, index] { UseModel(index); }),
        });
        if (form.provider == AiProvider::Ollama) {
            form.actions->Add(MakeButton("Get Efforts", [this, index] { StartTask(index, Task::Efforts); }));
        }
        form.actions->Add(MakeButton("Test Model", [this, index] { StartTask(index, Task::Test); }));
        form.actions->Add(MakeButton("Save", [this, index] { Save(index, false); }));
        form.actions->Add(MakeButton("Set Active", [this, index] { Save(index, true); }, ButtonRole::Primary));
        auto panel = Container::Vertical({});
        if (form.go_checkbox) {
            panel->Add(form.go_checkbox);
        }
        panel->Add(form.endpoint_input);
        if (form.key_input) {
            panel->Add(Container::Horizontal({form.key_input, form.key_visibility_button}));
        }
        panel->Add(form.model_input);
        if (form.effort_input) {
            panel->Add(form.effort_input);
        }
        panel->Add(form.models_menu);
        panel->Add(form.actions);
        panels->Add(panel);
    }
    stop_ = MakeButton("Cancel Task", [this] { Cancel(); }, ButtonRole::Warning);
    close_ = MakeButton("Close", std::move(close), ButtonRole::Cancel);
    copy_message_ = MakeButton("Copy Message", [this] { CopyMessage(); }, ButtonRole::Utility);
    close_info_ = MakeButton("Close", [this] { CloseInfo(); }, ButtonRole::Cancel);
    auto content = Container::Vertical({
        tabs_, panels, Container::Horizontal({copy_message_, Maybe(stop_, &busy_), close_}),
    });
    auto layers = Container::Tab({content, close_info_}, &info_panel_);
    container_ = CatchEvent(layers, [this](Event event) {
        if (info_panel_ && event == Event::Escape) {
            CloseInfo();
            return true;
        }
        return !info_panel_ && HandlePaste(event);
    });
}

AiSettingsModalContent::~AiSettingsModalContent() {
    cancelled_.store(true);
    command_.RequestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

Component AiSettingsModalContent::MakeButton(
    std::string label, std::function<void()> click, ButtonRole role, int tab) {
    auto spec = ButtonSpecFromLabel(std::move(label), role, ButtonVariant::AccentEdges, ButtonSize::Compact);
    ButtonOption option = ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(click);
    option.transform = [this, spec, tab](const EntryState& state) mutable {
        spec.selected = tab >= 0 && tab_ == tab;
        return RenderModalFlatButton(theme_, spec, state.focused || state.active);
    };
    return Button(option);
}

MenuOption AiSettingsModalContent::ConnectionMenu() const {
    auto option = MenuOption::Vertical();
    option.entries_option.transform = [this](const EntryState& state) {
        auto row = text(" " + state.label + " ");
        if (state.active) {
            return row | color(theme_.modal_selected_item_fg) | bgcolor(theme_.modal_selected_item_bg);
        }
        return row | color(theme_.modal_text_color);
    };
    return option;
}

Component AiSettingsModalContent::MakeInput(
    std::string& value, int& cursor, std::string placeholder, bool* password,
    std::function<void()> change) {
    InputOption option;
    option.multiline = false;
    if (password) {
        option.password = password;
    }
    option.transform = [this](InputState state) {
        return state.element | bgcolor(theme_.modal_input_bg) | color(theme_.modal_input_fg);
    };
    auto selection = std::make_shared<InputSelection>();
    input_selections_.push_back(selection);
    auto input = MakeSelectableInput(value, cursor, std::move(placeholder), std::move(option),
        std::move(selection));
    if (!change) {
        return input;
    }
    return CatchEvent(input, [input, &value, change = std::move(change)](Event event) {
        // FTXUI also fires on_change for caret moves with the mouse.
        if (!event.is_character() && event != Event::Backspace && event != Event::Delete) {
            return false;
        }
        const std::string before = value;
        input->OnEvent(std::move(event));
        if (value != before) {
            change();
        }
        return true;
    });
}

Component AiSettingsModalContent::MakeKeyVisibilityButton(ProviderForm& form) {
    auto option = ButtonOption::Simple();
    option.label = &form.key_visibility_label;
    option.on_click = [&form] {
        form.key_masked = !form.key_masked;
        form.key_visibility_label = form.key_masked ? "Show" : "Hide";
    };
    option.transform = [this](const EntryState& state) {
        const auto spec = ButtonSpecFromLabel(
            state.label, ButtonRole::Toggle, ButtonVariant::AccentEdges, ButtonSize::Compact);
        return RenderModalFlatButton(theme_, spec, state.focused || state.active);
    };
    return Button(std::move(option));
}

Component AiSettingsModalContent::MakeGoCheckbox(ProviderForm& form) {
    auto option = CheckboxOption::Simple();
    option.label = "Use Go subscription (unchecked: pay-as-you-go)";
    option.checked = &form.draft.opencode_use_go;
    option.on_change = [this, &form] {
        const auto endpoint = AiBackend::NormalizeServerUrl(form.draft.endpoint);
        if (endpoint.empty() || endpoint == AiBackend::DefaultEndpoint(form.provider, true) ||
            endpoint == AiBackend::DefaultEndpoint(form.provider, false)) {
            form.draft.endpoint = AiBackend::DefaultEndpoint(form.provider, form.draft.opencode_use_go);
        }
        InvalidateModels(form);
        SetMessage("OpenCode plan changed. Use Get Models to refresh the catalog.");
    };
    option.transform = [this](const EntryState& state) {
        auto row = text(std::string(state.state ? " [x] " : " [ ] ") + state.label);
        return state.focused
            ? row | color(theme_.modal_selected_item_fg) | bgcolor(theme_.modal_selected_item_bg)
            : row | color(theme_.modal_text_color);
    };
    return Checkbox(std::move(option));
}

Component AiSettingsModalContent::FocusedInput() const {
    if (tab_ == 0) {
        return {};
    }
    const auto& form = forms_[tab_ - 1];
    for (const auto& input : {form.endpoint_input, form.key_input, form.model_input, form.effort_input}) {
        if (input && input->Focused()) {
            return input;
        }
    }
    return {};
}

bool AiSettingsModalContent::HandlePaste(const Event& event) {
    if (event.input() == "\x1B[200~") {
        terminal_paste_active_ = true;
        terminal_paste_target_ = FocusedInput();
        terminal_paste_text_.clear();
        return true;
    }
    if (terminal_paste_active_) {
        if (event == Event::Custom || event.is_cursor_position()) {
            return false;
        }
        if (event.input() == "\x1B[201~") {
            terminal_paste_active_ = false;
            const auto input = std::exchange(terminal_paste_target_, {});
            auto text = std::exchange(terminal_paste_text_, {});
            if (input && input->Focused()) {
                PasteInto(input, std::move(text));
            }
        } else if (terminal_paste_target_ && !event.is_mouse()) {
            terminal_paste_text_ += event.input();
        }
        return true;
    }
    if (!ShortcutKeyMatchesEvent({ShortcutKeyModifier::Ctrl, "V"}, event) &&
        !ShortcutKeyMatchesEvent({ShortcutKeyModifier::CtrlShift, "V"}, event)) {
        return false;
    }
    const auto input = FocusedInput();
    if (!input || !read_clipboard_) {
        return false;
    }
    PasteInto(input, read_clipboard_());
    return true;
}

void AiSettingsModalContent::PasteInto(const Component& input, std::string text) {
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) {
        text.pop_back();
    }
    if (text.empty()) {
        SetMessage("Clipboard has no text to paste.", true);
        return;
    }
    if (std::any_of(text.begin(), text.end(),
            [](unsigned char ch) { return ch < ' ' || ch == 127; })) {
        SetMessage("Paste a single line into this field.", true);
        return;
    }
    input->OnEvent(Event::Character(std::move(text)));
    SetMessage("Pasted into the selected field.");
}

void AiSettingsModalContent::ResetInputState() {
    terminal_paste_active_ = false;
    terminal_paste_target_.reset();
    terminal_paste_text_.clear();
    for (const auto& selection : input_selections_) {
        *selection = {};
    }
    for (auto& form : forms_) {
        form.key_masked = true;
        form.key_visibility_label = "Show";
    }
}

void AiSettingsModalContent::Open() {
    ApplyTask();
    ResetInputState();
    info_panel_ = 0;
    messages_.assign(messages_.size(), {});
    for (auto& form : forms_) {
        form.draft = {};
        if (config_) {
            const auto found = config_->ai_providers.find(AiBackend::ProviderToConfig(form.provider));
            if (found != config_->ai_providers.end()) {
                form.draft = found->second;
            }
        }
        if (form.draft.endpoint.empty()) {
            form.draft.endpoint = AiBackend::DefaultEndpoint(form.provider, form.draft.opencode_use_go);
        }
        InvalidateModels(form);
        LoadEffort(form);
    }
    tab_ = 0;
    RefreshOverview();
    SetMessage("Select a provider to configure its connection and model.");
    overview_->TakeFocus();
}

void AiSettingsModalContent::Close() {
    ResetInputState();
    info_panel_ = 0;
    Cancel();
}

void AiSettingsModalContent::Cancel() {
    if (busy_) {
        cancelled_.store(true);
        command_.RequestStop();
        SetMessage("Stopping request…");
    }
}

void AiSettingsModalContent::SetMessage(std::string message, bool error, int tab) {
    messages_[tab < 0 ? tab_ : tab] = {std::move(message), {}, error};
}

Component AiSettingsModalContent::CurrentActions() const {
    return tab_ == 0 ? overview_actions_ : forms_[tab_ - 1].actions;
}

void AiSettingsModalContent::ShowInfo() {
    ResetInputState();
    info_panel_ = 1;
    close_info_->TakeFocus();
}

void AiSettingsModalContent::CloseInfo() {
    info_panel_ = 0;
    CurrentActions()->ChildAt(0)->TakeFocus();
}

void AiSettingsModalContent::CopyMessage() {
    auto& message = messages_[tab_];
    if (message.text.empty()) {
        message.copy_status = "No message";
    } else if (write_clipboard_ && write_clipboard_(message.text)) {
        message.copy_status = "Copied";
    } else {
        message.copy_status = "Copy failed";
    }
}

void AiSettingsModalContent::SelectTab(int tab) {
    if (tab < 0 || tab > static_cast<int>(forms_.size())) {
        return;
    }
    if (tab_ != tab) {
        ResetInputState();
        tab_ = tab;
    }
    if (tab_ == 0) {
        overview_->TakeFocus();
    } else {
        forms_[tab_ - 1].endpoint_input->TakeFocus();
    }
}

void AiSettingsModalContent::RefreshOverview() {
    overview_labels_.clear();
    for (const auto& form : forms_) {
        const auto id = AiBackend::ProviderToConfig(form.provider);
        const bool active = config_ && config_->ai_active_provider == id;
        std::string label = (active ? "* " : "  ") + AiBackend::ProviderLabel(form.provider);
        if (config_) {
            const auto saved = config_->ai_providers.find(id);
            if (saved != config_->ai_providers.end() && !saved->second.model.empty()) {
                label += "  |  " + saved->second.model;
            }
        }
        overview_labels_.push_back(std::move(label));
    }
}

void AiSettingsModalContent::InvalidateModels(ProviderForm& form) {
    ++form.revision;
    form.models.clear();
    form.model_labels.clear();
    form.selected_model = 0;
    form.inspected_model.reset();
}

void AiSettingsModalContent::LoadEffort(ProviderForm& form) {
    const auto found = form.draft.model_efforts.find(Trim(form.draft.model));
    form.effort = found == form.draft.model_efforts.end() ? std::string{} : found->second;
}

void AiSettingsModalContent::UseModel(size_t index) {
    auto& form = forms_[index];
    if (form.selected_model < 0 || static_cast<size_t>(form.selected_model) >= form.models.size()) {
        SetMessage("Get Models first, then select a model from the list.", true);
        return;
    }
    const auto& model = form.models[form.selected_model];
    if (Trim(form.draft.model) != model.id) {
        form.draft.model = model.id;
        LoadEffort(form);
    }
    SetMessage("Selected " + model.id + ". Save or set it active to apply.");
}

void AiSettingsModalContent::Save(size_t index, bool activate, bool saved_only) {
    if (!config_ || index >= forms_.size()) {
        SetMessage("AI settings are unavailable.", true);
        return;
    }
    auto& form = forms_[index];
    const auto key = AiBackend::ProviderToConfig(form.provider);
    const auto existing = config_->ai_providers.find(key);
    if (saved_only && (existing == config_->ai_providers.end() || existing->second.model.empty())) {
        SetMessage("Configure and save a model for this provider first.", true);
        return;
    }
    if (saved_only) {
        const auto previous = config_->ai_active_provider;
        config_->ai_active_provider = key;
        const std::string error = AiBackend::ValidationError(ActiveAiSettings(*config_));
        if (!error.empty() || !config_->Persist()) {
            config_->ai_active_provider = previous;
            SetMessage(error.empty() ? "Cannot save AI settings." : error, true);
            return;
        }
        RefreshOverview();
        SetMessage("Active model: " + existing->second.model + ".");
        return;
    }

    const auto settings = form.Settings();
    const auto error = AiBackend::ValidationError(settings);
    if (!error.empty()) {
        SetMessage(error, true);
        return;
    }
    if ((activate || config_->ai_active_provider == key) && settings.model.empty()) {
        SetMessage("Choose a model or enter its model ID first.", true);
        return;
    }
    const auto discovered = std::find_if(form.models.begin(), form.models.end(),
        [&](const AiModelInfo& model) { return model.id == settings.model; });
    const AiModelInfo* metadata = discovered != form.models.end() ? &*discovered : nullptr;
    if (form.inspected_model && form.inspected_model->id == settings.model) {
        metadata = &*form.inspected_model;
    }
    if (!settings.effort.empty() && metadata && !metadata->efforts.empty() &&
        std::find(metadata->efforts.begin(), metadata->efforts.end(), settings.effort) ==
            metadata->efforts.end()) {
        SetMessage("Choose one of the efforts listed for this model.", true);
        return;
    }

    AiProviderConfig saved = form.draft;
    saved.endpoint = settings.server_url;
    saved.api_key = settings.api_key;
    saved.model = settings.model;
    if (!saved.model.empty()) {
        if (settings.effort.empty()) {
            saved.model_efforts.erase(saved.model);
        } else {
            saved.model_efforts[saved.model] = settings.effort;
        }
    }
    const auto previous = config_->ai_active_provider;
    const auto previous_profile = existing == config_->ai_providers.end()
        ? std::nullopt : std::optional<AiProviderConfig>(existing->second);
    config_->ai_providers[key] = saved;
    if (activate) {
        config_->ai_active_provider = key;
    }
    if (!config_->Persist()) {
        if (previous_profile) {
            config_->ai_providers[key] = *previous_profile;
        } else {
            config_->ai_providers.erase(key);
        }
        config_->ai_active_provider = previous;
        SetMessage("Cannot save AI settings.", true);
        return;
    }
    form.draft = std::move(saved);
    LoadEffort(form);
    RefreshOverview();
    SetMessage(activate ? "Active model: " + settings.model + "." : "Provider settings saved.");
}

void AiSettingsModalContent::StartTask(size_t index, Task task) {
    ApplyTask();
    if (busy_) {
        SetMessage("An AI settings request is already running.", true);
        return;
    }
    auto settings = forms_[index].Settings();
    if (task != Task::Test) {
        settings.effort.clear();
    }
    const auto error = AiBackend::ValidationError(settings);
    if (!error.empty() || (task != Task::Models && settings.model.empty())) {
        SetMessage(error.empty() ? "Choose a model first." : error, true);
        return;
    }
    settings.timeout_seconds = task == Task::Test ? 60 : 20;
    cancelled_.store(false);
    command_.Reset();
    busy_ = true;
    SetMessage(task == Task::Models ? "Fetching models…" :
        task == Task::Efforts ? "Checking model effort levels…" : "Testing model with sample text…");
    const size_t revision = forms_[index].revision;
    worker_ = std::thread([this, index, revision, task, settings = std::move(settings)] {
        TaskResult result{index, revision, task, {}, false, {}};
        try {
            const AiBackend backend(settings);
            if (task != Task::Test) {
                auto connection = task == Task::Efforts
                    ? backend.GetOllamaModelEfforts(&cancelled_, &command_)
                    : backend.CheckConnectionAndListModels(&cancelled_, &command_);
                result.success = connection.success;
                result.models = std::move(connection.models);
                if (!result.success) {
                    result.message = std::move(connection.error);
                } else if (task == Task::Efforts) {
                    result.message = result.models.front().efforts.empty()
                        ? "This Ollama server did not report effort levels for the model."
                        : "Effort levels loaded for " + settings.model + ".";
                } else {
                    result.message = std::to_string(result.models.size()) + " model(s) found.";
                }
            } else {
                const AiPromptRequest request{
                    "Reply with one short sentence.",
                    "Confirm that the model is responding.",
                };
                const auto generated = backend.Run(request, &cancelled_, {}, &command_);
                result.success = generated.success;
                result.message = result.success ? "Model test completed." : generated.error;
            }
        } catch (const std::exception&) {
            result.message = "The AI request could not be completed.";
        }
        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            pending_ = std::move(result);
        }
        if (redraw_) {
            redraw_();
        }
    });
}

void AiSettingsModalContent::ApplyTask() {
    std::optional<TaskResult> result;
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        result.swap(pending_);
    }
    if (!result) {
        return;
    }
    worker_.join();
    busy_ = false;
    const int result_tab = static_cast<int>(result->provider_index + 1);
    if (cancelled_.load()) {
        SetMessage("Request cancelled.", false, result_tab);
        return;
    }
    auto& form = forms_[result->provider_index];
    if (form.revision != result->revision) {
        SetMessage("Connection changed while the request was running. Try again.", false, result_tab);
        return;
    }
    if (result->task == Task::Efforts && result->success) {
        form.inspected_model = std::move(result->models.front());
    }
    if (result->task == Task::Models && result->success) {
        form.models = std::move(result->models);
        form.model_labels.clear();
        form.selected_model = 0;
        for (const auto& model : form.models) {
            if (model.id == Trim(form.draft.model)) {
                form.selected_model = static_cast<int>(form.model_labels.size());
            }
            form.model_labels.push_back(model.title == model.id ? model.id :
                model.title + "  |  " + model.id);
        }
    }
    SetMessage(std::move(result->message), !result->success, result_tab);
}

Element AiSettingsModalContent::RenderOverview() {
    const auto& form = forms_[selected_provider_];
    const auto key = AiBackend::ProviderToConfig(form.provider);
    const AiProviderConfig* saved = nullptr;
    if (config_) {
        const auto found = config_->ai_providers.find(key);
        if (found != config_->ai_providers.end()) {
            saved = &found->second;
        }
    }
    const std::string model = saved && !saved->model.empty() ? saved->model : "None selected";
    const std::string endpoint = saved && !saved->endpoint.empty()
        ? saved->endpoint : AiBackend::DefaultEndpoint(form.provider, !saved || saved->opencode_use_go);
    return hbox({
        vbox({
            text(" Connections ") | bold | color(theme_.modal_accent),
            separator() | color(theme_.modal_border),
            overview_->Render() | vscroll_indicator | frame | flex,
        }) | borderStyled(LIGHT, theme_.modal_border) | size(WIDTH, EQUAL, 37),
        vbox({
            text(" " + AiBackend::ProviderLabel(form.provider) + " ") | bold | color(theme_.modal_accent),
            separator() | color(theme_.modal_border),
            paragraph(" Model: " + model),
            paragraph(" " + std::string(form.provider == AiProvider::ChatGpt ? "Executable: " : "Endpoint: ") + endpoint),
            form.provider == AiProvider::OpenCodeGo
                ? text(saved && !saved->opencode_use_go ? " Plan: Pay-as-you-go" : " Plan: Go")
                : emptyElement(),
            NeedsKey(form.provider)
                ? text(saved && !saved->api_key.empty() ? " API key: saved" : " API key: required")
                : text(""),
            filler(),
        }) | borderStyled(LIGHT, theme_.modal_border) | flex,
    }) | flex;
}

Element AiSettingsModalContent::RenderProvider(const ProviderForm& form) {
    auto field = [&](const std::string& label, const Component& component, const Component& action = {}) {
        return hbox({
            text(" " + label) | size(WIDTH, EQUAL, 17) | color(theme_.modal_accent),
            component->Render() | flex | bgcolor(theme_.modal_input_bg),
            action ? hbox({text(" "), action->Render()}) : emptyElement(),
        });
    };
    Elements fields;
    if (form.go_checkbox) {
        fields.push_back(hbox({form.go_checkbox->Render(), filler()}));
    }
    fields.push_back(field(form.provider == AiProvider::ChatGpt ? "Codex executable" : "Server URL",
        form.endpoint_input));
    if (form.provider == AiProvider::ChatGpt) {
        fields.push_back(paragraph(" Sign in with ChatGPT using codex login, then select Get Models.") | dim);
    } else if (NeedsKey(form.provider)) {
        fields.push_back(field("API key", form.key_input, form.key_visibility_button));
    }
    fields.push_back(field("Model ID", form.model_input));
    if (form.provider != AiProvider::OpenAiCompatible) {
        fields.push_back(field(form.provider == AiProvider::Mimo ? "Thinking" : "Effort", form.effort_input));
        std::string hint;
        switch (form.provider) {
            case AiProvider::Ollama: hint = "Leave blank for default, or enter a level supported by the model."; break;
            case AiProvider::OpenCodeGo: hint = "Leave blank for default, or enter a supported reasoning effort."; break;
            case AiProvider::Claude: hint = "Supported models accept low, medium, high, xhigh, or max."; break;
            case AiProvider::Mimo: hint = "Leave blank for default, or enter enabled or disabled."; break;
            case AiProvider::ChatGpt: hint = "Leave blank for the model’s default effort."; break;
            default: break;
        }
        const std::string current_model = Trim(form.draft.model);
        const auto discovered = std::find_if(form.models.begin(), form.models.end(),
            [&](const AiModelInfo& model) { return model.id == current_model; });
        const AiModelInfo* metadata = discovered != form.models.end() ? &*discovered : nullptr;
        if (form.inspected_model && form.inspected_model->id == current_model) {
            metadata = &*form.inspected_model;
        }
        if (metadata && !metadata->efforts.empty()) {
            hint = "Available effort levels:";
            for (const auto& effort : metadata->efforts) {
                hint += " " + effort;
            }
        }
        fields.push_back(paragraph(" " + hint) | dim);
    }
    fields.push_back(separator() | color(theme_.modal_border));
    fields.push_back(text(" Available models ") | bold | color(theme_.modal_accent));
    fields.push_back(form.models.empty()
        ? text(" Select Get Models to fetch the list.") | dim | flex
        : form.models_menu->Render() | vscroll_indicator | frame | flex);
    return vbox(std::move(fields)) | borderStyled(LIGHT, theme_.modal_border) | flex;
}

Element AiSettingsModalContent::RenderInfo() {
    std::string title = "Providers";
    std::vector<std::string> paragraphs;
    if (tab_ == 0) {
        paragraphs = {
            "The list shows each provider and its saved model. An asterisk marks the active provider.",
            "Configure opens the selected provider tab. Set Active switches to its saved model.",
            "Copy Message copies the full status or error from the current tab.",
        };
    } else {
        const auto provider = forms_[tab_ - 1].provider;
        title = AiBackend::ProviderLabel(provider);
        switch (provider) {
            case AiProvider::Ollama:
                paragraphs = {
                    "Install Ollama on the machine that runs the server. Start the Ollama app or run ollama serve.",
                    "In another terminal, run ollama pull <model> or ollama list to see what is already installed.",
                    "For Ollama on the same machine, use http://localhost:11434/v1. If TextLT runs in WSL and Ollama runs in Windows, use the Windows host address if localhost cannot reach it.",
                    "Get Models lists installed models. Get Efforts checks the selected model's supported thinking controls.",
                };
                break;
            case AiProvider::OpenCodeGo:
                paragraphs = {
                    "Check Use Go subscription for a Go workspace. Clear it to use credits in a pay-as-you-go workspace.",
                    "Server URL changes with the plan when you use the standard OpenCode endpoint. Enter a Console API key from the workspace for that plan.",
                    "Get Models loads the selected plan's catalog. Leave Effort blank for the model default. Free models restricted to the OpenCode app cannot be tested here.",
                };
                break;
            case AiProvider::ChatGpt:
                paragraphs = {
                    "Install the Codex CLI and run codex login. Sign in with your ChatGPT account.",
                    "Codex executable is the command name (usually codex) or the path to the executable. Get Models reads the signed-in catalog from Codex app-server.",
                    "Effort controls reasoning for the selected model. Use Test Model to check that your account can run it.",
                };
                break;
            case AiProvider::Claude:
                paragraphs = {
                    "Server URL is the Anthropic API base, normally https://api.anthropic.com.",
                    "Enter an Anthropic Console API key. Get Models lists models available to that key.",
                    "Effort controls reasoning on supported models. Leave it blank to use the provider default.",
                };
                break;
            case AiProvider::Mimo:
                paragraphs = {
                    "Server URL is the MiMo API base, normally https://api.xiaomimimo.com/v1.",
                    "Enter a Xiaomi MiMo API key and choose a text model from Get Models.",
                    "Thinking accepts enabled or disabled. Leave it blank to use the model default.",
                };
                break;
            case AiProvider::OpenAiCompatible:
                paragraphs = {
                    "Server URL is the base of your OpenAI-compatible server. Get Models uses its /v1/models endpoint.",
                    "TextLT sends test prompts to /v1/chat/completions. Use a server that supports streamed text responses.",
                };
                break;
            case AiProvider::Auto:
                break;
        }
        paragraphs.push_back(
            "Use Model fills Model ID from the list, or enter an ID directly. Test Model sends a sample request. Save keeps these settings, while Set Active also selects this provider.");
    }

    Elements rows{
        text(" " + title + " Info ") | bold | color(theme_.modal_accent),
        separator() | color(theme_.modal_border),
    };
    for (const auto& section : paragraphs) {
        rows.push_back(paragraph(section));
        rows.push_back(text(""));
    }
    rows.push_back(hbox({filler(), close_info_->Render()}));
    return vbox(std::move(rows)) | borderStyled(LIGHT, theme_.modal_border) |
        size(WIDTH, LESS_THAN, 78) | clear_under |
        bgcolor(theme_.modal_background) | color(theme_.modal_text_color);
}

Element AiSettingsModalContent::Render() {
    ApplyTask();
    Elements tabs;
    for (size_t i = 0; i < tabs_->ChildCount(); ++i) {
        tabs.push_back(tabs_->ChildAt(i)->Render());
        tabs.push_back(text(" "));
    }
    auto body = vbox({
        hbox(std::move(tabs)),
        separator() | color(theme_.modal_border),
        tab_ == 0 ? RenderOverview() : RenderProvider(forms_[tab_ - 1]),
    }) | bgcolor(theme_.modal_background) | color(theme_.modal_text_color);
    if (info_panel_) {
        return dbox({body, RenderInfo() | center});
    }
    return body;
}

Element AiSettingsModalContent::RenderCustomFooter() {
    const auto actions = CurrentActions();
    const auto& message = messages_[tab_];
    Elements buttons;
    if (!info_panel_) {
        for (size_t i = 0; i < actions->ChildCount(); ++i) {
            buttons.push_back(actions->ChildAt(i)->Render());
            buttons.push_back(text(" "));
        }
        buttons.push_back(filler());
        buttons.push_back(close_->Render());
    }
    return vbox({
        hbox({
            text(" "),
            paragraph(message.text) | yframe | flex |
                color(message.error ? theme_.button_danger : theme_.modal_text_color),
            info_panel_ ? emptyElement() : vbox({
                hbox({
                    copy_message_->Render(),
                    busy_ ? hbox({text(" "), stop_->Render()}) : emptyElement(),
                }),
                text(message.copy_status) | color(theme_.modal_accent) | center,
            }),
        }) | size(HEIGHT, EQUAL, 2),
        separator() | color(theme_.modal_border),
        hbox(std::move(buttons)) | size(HEIGHT, EQUAL, 1),
    });
}

AiSettingsModal::AiSettingsModal(
    const Theme* theme, EditorConfig* config, std::function<void()> request_redraw,
    std::function<std::string()> read_clipboard,
    std::function<bool(const std::string&)> write_clipboard) {
    content_ = std::make_shared<AiSettingsModalContent>(
        theme, config, std::move(request_redraw), [this] { Close(); },
        std::move(read_clipboard), std::move(write_clipboard));
    modal_ = std::make_shared<ModalWindow>(content_, theme, [this] { Close(); });
    modal_->SetBodyFrameScrolling(false);
}

Component AiSettingsModal::View() const { return modal_; }

void AiSettingsModal::Open() {
    open_ = true;
    content_->Open();
}

void AiSettingsModal::Close() {
    content_->Close();
    open_ = false;
}

bool AiSettingsModal::IsOpen() const { return open_; }

bool AiSettingsModal::OnEvent(Event event) {
    return open_ && modal_->OnEvent(std::move(event));
}

} // namespace textlt
