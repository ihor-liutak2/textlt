#include "modals/modal_ai_settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include <archive.h>
#include <archive_entry.h>

#include "ai/local_llama_server.hpp"
#include "curl_manager.hpp"
#include "ftxui/component/component_options.hpp"
#include "json_utils.hpp"
#include "modals/assistant_modals.hpp"
#include "remote/remote_http_client.hpp"
#include "ui_button.hpp"

namespace textlt {
namespace {

using assistant_modal_detail::DownloadCacheDirectory;
using assistant_modal_detail::DownloadRegistry;
using assistant_modal_detail::EnsureDirectory;
using assistant_modal_detail::JsonLabel;
using assistant_modal_detail::LoadUserRegistryJson;
using assistant_modal_detail::RegistryFilename;
using assistant_modal_detail::RegistryKind;
using assistant_modal_detail::RegistryLoadResult;
using assistant_modal_detail::UserDataDirectory;

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
    if (result.model_load_ms > 0.0) {
        status += " · load " + FormatAiSeconds(result.model_load_ms) + " s";
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

std::filesystem::path RuntimeDirectory() {
    return UserDataDirectory() / "ai" / "runtimes" / "llama_cpp";
}

std::filesystem::path ModelsDirectory() {
    return UserDataDirectory() / "ai" / "models";
}

std::filesystem::path FindInstalledRuntimeBinary() {
    std::error_code error;
    const std::filesystem::path directory = RuntimeDirectory();
    if (!std::filesystem::exists(directory, error)) {
        return {};
    }
#ifdef _WIN32
    const std::vector<std::string> preferred_names = {"llama-server.exe"};
#else
    const std::vector<std::string> preferred_names = {"llama-server"};
#endif
    for (const std::string& wanted : preferred_names) {
        error.clear();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error)) {
            if (error) {
                return {};
            }
            if (entry.is_regular_file() && entry.path().filename() == wanted) {
                return entry.path();
            }
        }
    }
    return {};
}

bool RuntimeBinaryExists() {
    return !FindInstalledRuntimeBinary().empty();
}

std::string RuntimeDisplayPath() {
    const std::filesystem::path installed = FindInstalledRuntimeBinary();
    if (!installed.empty()) {
        return installed.string();
    }
#ifdef _WIN32
    return "%LOCALAPPDATA%\\textlt\\ai\\runtimes\\llama_cpp\\llama-server.exe";
#else
    return "~/.local/share/textlt/ai/runtimes/llama_cpp/llama-server";
#endif
}

std::string CurrentPlatform() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

std::string CurrentArchitecture() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x64";
#else
    return {};
#endif
}

std::string RuntimeDownloadUrl() {
    Json root;
    if (LoadUserRegistryJson(RegistryKind::Ai, &root) != RegistryLoadResult::Loaded) {
        return {};
    }
    const std::string backend_id = JsonString(root, "default_backend", "llama_cpp");
    const std::string runtime_key = CurrentPlatform() + "_" + CurrentArchitecture();
    const auto backends = root.find("backends");
    if (CurrentArchitecture().empty() || backends == root.end() || !backends->is_array()) {
        return {};
    }
    for (const Json& backend : *backends) {
        if (!backend.is_object() || JsonString(backend, "id") != backend_id) {
            continue;
        }
        const auto runtime = backend.find("runtime");
        if (runtime == backend.end() || !runtime->is_object()) {
            return {};
        }
        const auto urls = runtime->find("runtime_download_urls");
        if (urls == runtime->end() || !urls->is_object()) {
            return {};
        }
        return JsonString(*urls, runtime_key.c_str());
    }
    return {};
}

bool IsSafeArchivePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    for (const auto& part : path) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

bool CopyArchiveData(archive* input, archive* output) {
    const void* buffer = nullptr;
    size_t size = 0;
    la_int64_t offset = 0;
    while (true) {
        const int result = archive_read_data_block(input, &buffer, &size, &offset);
        if (result == ARCHIVE_EOF) {
            return true;
        }
        if (result != ARCHIVE_OK ||
            archive_write_data_block(output, buffer, size, offset) != ARCHIVE_OK) {
            return false;
        }
    }
}

bool ExtractArchive(
    const std::filesystem::path& archive_path,
    const std::filesystem::path& destination) {
    EnsureDirectory(destination);
    archive* input = archive_read_new();
    archive* output = archive_write_disk_new();
    if (!input || !output) {
        if (input) archive_read_free(input);
        if (output) archive_write_free(output);
        return false;
    }
    archive_read_support_filter_all(input);
    archive_read_support_format_all(input);
    archive_write_disk_set_options(
        output,
        ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_SYMLINKS);
    if (archive_read_open_filename(input, archive_path.string().c_str(), 10240) != ARCHIVE_OK) {
        archive_read_free(input);
        archive_write_free(output);
        return false;
    }

    bool ok = true;
    archive_entry* entry = nullptr;
    while (archive_read_next_header(input, &entry) == ARCHIVE_OK) {
        const std::filesystem::path relative = archive_entry_pathname(entry)
            ? archive_entry_pathname(entry)
            : "";
        if (!IsSafeArchivePath(relative)) {
            archive_read_data_skip(input);
            continue;
        }
        const std::string full = (destination / relative).string();
        archive_entry_set_pathname(entry, full.c_str());
        const int header = archive_write_header(output, entry);
        if ((header != ARCHIVE_OK && header != ARCHIVE_WARN) ||
            (archive_entry_size(entry) > 0 && !CopyArchiveData(input, output)) ||
            archive_write_finish_entry(output) != ARCHIVE_OK) {
            ok = false;
            break;
        }
    }
    archive_read_close(input);
    archive_read_free(input);
    archive_write_close(output);
    archive_write_free(output);
    return ok;
}

bool DownloadFile(
    const std::string& url,
    const std::filesystem::path& final_path,
    const std::atomic<bool>* cancel_requested,
    RemoteCommandControl* command_control) {
    EnsureDirectory(final_path.parent_path());
    const std::filesystem::path part_path = final_path.string() + ".part";
    std::error_code error;
    std::filesystem::remove(part_path, error);
    std::vector<std::string> headers;
    const char* hf_token = std::getenv("HF_TOKEN");
    if (hf_token && *hf_token && url.find("huggingface.co/") != std::string::npos) {
        headers.push_back(std::string("Authorization: Bearer ") + hf_token);
    }
    RemoteHttpClient client;
    const RemoteHttpResponse response = client.DownloadCancelable(
        "GET", url, headers, part_path, {}, 0, cancel_requested, command_control);
    if (!response.ok) {
        std::filesystem::remove(part_path, error);
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
        return false;
    }
    return true;
}

std::vector<AiModelInfo> LoadLocalModels(std::vector<std::string>* descriptions) {
    std::vector<AiModelInfo> models = AiBackend::LoadManagedLocalModels();
    if (descriptions) {
        descriptions->reserve(descriptions->size() + models.size());
        for (const AiModelInfo& model : models) {
            descriptions->push_back(model.description);
        }
    }
    return models;
}

std::string ModelStatus(const AiModelInfo& model) {
    if (model.source == AiModelSource::Server) {
        return model.available ? "available" : "offline";
    }
    return model.downloaded ? "downloaded" : "not downloaded";
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
    server_url_input_ = ftxui::Input(&server_url_, "http://127.0.0.1:11434", input_option);

    provider_ollama_button_ = MakeButton(
        &theme_, "Ollama", [this] { SelectProvider(AiProvider::Ollama); },
        ButtonRole::Toggle,
        [this] { return provider_ == AiProvider::Ollama; });
    provider_openai_button_ = MakeButton(
        &theme_, "OpenAI-compatible", [this] { SelectProvider(AiProvider::OpenAiCompatible); },
        ButtonRole::Toggle,
        [this] { return provider_ == AiProvider::OpenAiCompatible; });
    provider_local_button_ = MakeButton(
        &theme_, "Local llama.cpp", [this] { SelectProvider(AiProvider::LocalLlamaCpp); },
        ButtonRole::Toggle,
        [this] { return provider_ == AiProvider::LocalLlamaCpp; });
    fetch_registry_button_ = MakeButton(
        &theme_, "Fetch registry", [this] { FetchAiRegistry(); });
    runtime_download_button_ = MakeButton(
        &theme_, "Download runtime", [this] { DownloadRuntime(); });
    runtime_delete_button_ = MakeButton(
        &theme_, "Delete runtime", [this] { RequestRuntimeDelete(); });
    runtime_confirm_delete_button_ = MakeButton(
        &theme_, "Confirm delete", [this] { ConfirmRuntimeDelete(); });
    runtime_cancel_delete_button_ = MakeButton(
        &theme_, "Cancel", [this] { CancelRuntimeDelete(); });
    model_download_button_ = MakeButton(
        &theme_, "Download model", [this] { DownloadSelectedModel(); });
    model_delete_button_ = MakeButton(
        &theme_, "Delete model", [this] { RequestSelectedModelDelete(); });
    model_confirm_delete_button_ = MakeButton(
        &theme_, "Confirm delete", [this] { ConfirmSelectedModelDelete(); });
    model_cancel_delete_button_ = MakeButton(
        &theme_, "Cancel", [this] { CancelSelectedModelDelete(); });
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
            provider_ollama_button_, provider_openai_button_, provider_local_button_,
        }),
        ftxui::Container::Horizontal({
            runtime_download_button_, runtime_delete_button_, fetch_registry_button_,
            model_download_button_, model_delete_button_, test_button_,
        }),
        ftxui::Container::Horizontal({
            runtime_confirm_delete_button_, runtime_cancel_delete_button_,
            model_confirm_delete_button_, model_cancel_delete_button_,
        }),
        model_menu_,
    });
    auto test_content = ftxui::Container::Vertical({close_test_button_});
    auto panels = ftxui::Container::Tab({main_content, test_content}, &active_panel_);
    container_ = ftxui::CatchEvent(panels, [this](ftxui::Event event) {
        if (event == ftxui::Event::Escape) {
            bool test_popup_visible = false;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                test_popup_visible = test_popup_visible_;
            }
            if (test_popup_visible) {
                CloseTestPopup();
                return true;
            }
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
    LoadModels();
}

void AiSettingsModalContent::LoadModels() {
    const std::string selected_key = config_ ? config_->ai_selected_model_key : std::string{};
    model_descriptions_.clear();
    models_ = LoadLocalModels(&model_descriptions_);
    for (const AiModelInfo& model : server_models_) {
        models_.push_back(model);
        model_descriptions_.push_back("Model reported by the configured AI server.");
    }

    const auto selected_iter = std::find_if(
        models_.begin(), models_.end(), [&](const AiModelInfo& model) {
            return model.key == selected_key;
        });
    if (!selected_key.empty() && selected_iter == models_.end()) {
        AiModelInfo placeholder;
        placeholder.key = selected_key;
        placeholder.id = AiBackend::ModelIdFromKey(selected_key);
        placeholder.title = placeholder.id;
        bool recognized_key = false;
        if (selected_key.rfind("ollama:", 0) == 0) {
            placeholder.provider_label = "Ollama";
            placeholder.source = AiModelSource::Server;
            recognized_key = true;
        } else if (selected_key.rfind("openai:", 0) == 0) {
            placeholder.provider_label = "OpenAI-compatible";
            placeholder.source = AiModelSource::Server;
            recognized_key = true;
        }
        if (recognized_key && !placeholder.title.empty()) {
            models_.push_back(std::move(placeholder));
            model_descriptions_.push_back(
                "Previously selected model. Refresh its server or registry to update availability.");
        }
    }

    model_labels_.clear();
    for (const AiModelInfo& model : models_) {
        std::string hardware;
        if (model.gpu_required) {
            hardware = " | GPU " + std::to_string(model.recommended_vram_mb / 1024) +
                " GB VRAM recommended";
        }
        if (!model.tier.empty()) {
            hardware += " | " + model.tier;
        }
        model_labels_.push_back(
            model.title + " | " + model.provider_label + hardware + " | " + ModelStatus(model));
    }
    if (model_labels_.empty()) {
        model_labels_.push_back("No models. Fetch the registry or connect to a server.");
        selected_model_ = 0;
        persisted_model_index_ = -1;
        return;
    }

    selected_model_ = 0;
    bool selection_found = false;
    for (size_t index = 0; index < models_.size(); ++index) {
        if (models_[index].key == selected_key) {
            selected_model_ = static_cast<int>(index);
            selection_found = true;
            break;
        }
    }
    if (!selection_found) {
        const auto preferred = std::find_if(
            models_.begin(), models_.end(), [](const AiModelInfo& model) {
                return model.available || model.downloaded;
            });
        if (preferred != models_.end()) {
            selected_model_ = static_cast<int>(
                std::distance(models_.begin(), preferred));
        }
        persisted_model_index_ = -1;
        return;
    }
    persisted_model_index_ = selected_model_;
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
            if (!unload_in_progress_) {
                busy_ = false;
                operation_state_ = OperationState::Idle;
            }
        }
        RequestRedraw();
    });
}

bool AiSettingsModalContent::SaveConnectionSettings() {
    server_url_ = AiBackend::NormalizeServerUrl(server_url_);
    if (server_url_.empty() && provider_ != AiProvider::LocalLlamaCpp) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "AI server URL cannot be empty.";
        return false;
    }
    if (provider_ != AiProvider::LocalLlamaCpp &&
        !AiBackend::IsSupportedServerUrl(server_url_)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "AI server URL must start with http:// or https://.";
        return false;
    }
    bool connection_changed = false;
    if (config_) {
        const std::string provider_value = AiBackend::ProviderToConfig(provider_);
        connection_changed =
            config_->ai_server_url != server_url_ || config_->ai_provider != provider_value;
        config_->ai_server_url = server_url_;
        config_->ai_provider = provider_value;
        if (!config_->Persist()) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            status_ = "Could not save AI settings.";
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    status_ = "AI connection settings saved.";
    if (config_ && connection_changed) {
        connection_status_ = "Not checked after settings change";
    }
    return true;
}

void AiSettingsModalContent::SelectProvider(AiProvider provider) {
    provider_ = provider;
    std::lock_guard<std::mutex> lock(state_mutex_);
    connection_status_ = "Not checked after provider change";
    status_ = "Provider selected. Use Save or Connect / Refresh to apply it.";
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
        status_ = provider_ == AiProvider::LocalLlamaCpp
            ? "Checking local llama.cpp runtime..."
            : "Connecting to AI server...";
        connection_status_ = provider_ == AiProvider::LocalLlamaCpp
            ? "Checking local llama.cpp"
            : "Connecting to " + AiBackend::ProviderLabel(provider_);
    }
    StartWorker([this, settings] {
        const AiConnectionResult result =
            AiBackend(settings).CheckConnectionAndListModels(&cancel_requested_, &command_control_);
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_server_models_ = true;
        pending_connection_success_ = result.success;
        pending_detected_provider_ = result.provider;
        if (cancel_requested_.load()) {
            pending_status_ = "Connection check stopped.";
        } else if (!result.success) {
            pending_status_ = result.error;
        } else if (result.provider == AiProvider::LocalLlamaCpp) {
            pending_status_ = "llama.cpp runtime is ready. Managed local models are shown below.";
        } else {
            pending_status_ = result.provider_label + " connected. " +
                std::to_string(result.models.size()) + " model(s) found.";
        }
        pending_models_ = result.models;
    });
}

void AiSettingsModalContent::FetchAiRegistry() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "Fetching AI registry...";
    }
    StartWorker([this] {
        const auto result = DownloadRegistry(
            CurlManager::kAiRegistryUrl,
            RegistryFilename(RegistryKind::Ai),
            &cancel_requested_,
            &command_control_);
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_reload_ = result == assistant_modal_detail::RegistryDownloadResult::Saved;
        pending_status_ = cancel_requested_.load()
            ? "AI registry download stopped."
            : (pending_reload_ ? "AI registry loaded." : "AI registry download failed.");
    });
}

void AiSettingsModalContent::DownloadRuntime() {
    const std::string url = RuntimeDownloadUrl();
    if (url.empty()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "No runtime package exists for " + CurrentPlatform() + " " +
            CurrentArchitecture() + ".";
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "Downloading llama.cpp runtime...";
        runtime_delete_confirmation_ = false;
    }
    StartWorker([this, url] {
        const std::filesystem::path archive_path =
            DownloadCacheDirectory() / std::filesystem::path(url).filename();
        const bool downloaded = DownloadFile(url, archive_path, &cancel_requested_, &command_control_);
        bool installed = false;
        if (downloaded) {
            LocalLlamaServerManager::Instance().Unload();
            std::error_code error;
            std::filesystem::remove_all(RuntimeDirectory(), error);
            installed = ExtractArchive(archive_path, RuntimeDirectory()) && RuntimeBinaryExists();
            std::filesystem::remove(archive_path, error);
        }
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_reload_ = true;
        pending_status_ = cancel_requested_.load()
            ? "Runtime download stopped."
            : (installed ? "llama.cpp runtime installed." : "Runtime installation failed.");
    });
}

void AiSettingsModalContent::RequestRuntimeDelete() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!RuntimeBinaryExists()) {
        status_ = "llama.cpp runtime is not installed.";
        return;
    }
    runtime_delete_confirmation_ = true;
    model_delete_confirmation_ = false;
    status_ = "Confirm llama.cpp runtime deletion.";
}

void AiSettingsModalContent::ConfirmRuntimeDelete() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        runtime_delete_confirmation_ = false;
        status_ = "Deleting llama.cpp runtime...";
    }
    StartWorker([this] {
        LocalLlamaServerManager::Instance().Unload();
        std::error_code error;
        std::filesystem::remove_all(RuntimeDirectory(), error);
        const bool deleted = !error && !RuntimeBinaryExists();
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_reload_ = true;
        pending_status_ = deleted ? "llama.cpp runtime deleted." : "Runtime deletion failed.";
    });
}

void AiSettingsModalContent::CancelRuntimeDelete() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    runtime_delete_confirmation_ = false;
    status_ = "Runtime deletion cancelled.";
}

void AiSettingsModalContent::DownloadSelectedModel() {
    if (selected_model_ < 0 || static_cast<size_t>(selected_model_) >= models_.size()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "No model selected.";
        return;
    }
    const AiModelInfo selected = models_[static_cast<size_t>(selected_model_)];
    if (selected.source != AiModelSource::ManagedLocal) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "Server models are managed by the AI server, not downloaded by TextLT.";
        return;
    }
    const std::string url = selected.model_url;
    if (url.empty()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "Selected model has no download URL.";
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "Downloading " + selected.title + "...";
        model_delete_confirmation_ = false;
    }
    StartWorker([this, url, selected] {
        const bool downloaded = DownloadFile(
            url, ModelsDirectory() / selected.filename, &cancel_requested_, &command_control_);
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_reload_ = true;
        pending_status_ = cancel_requested_.load()
            ? "Model download stopped."
            : (downloaded
                ? "Model downloaded."
                : "Model download failed. Gemma files may require accepting the Hugging Face license and setting HF_TOKEN.");
    });
}

void AiSettingsModalContent::RequestSelectedModelDelete() {
    if (selected_model_ < 0 || static_cast<size_t>(selected_model_) >= models_.size()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "No model selected.";
        return;
    }
    const AiModelInfo& selected = models_[static_cast<size_t>(selected_model_)];
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (selected.source != AiModelSource::ManagedLocal) {
        status_ = "Server models must be removed in the AI server.";
        return;
    }
    if (!selected.downloaded) {
        status_ = "Selected model is not downloaded.";
        return;
    }
    pending_model_delete_filename_ = selected.filename;
    model_delete_confirmation_ = true;
    runtime_delete_confirmation_ = false;
    status_ = "Confirm selected model deletion.";
}

void AiSettingsModalContent::ConfirmSelectedModelDelete() {
    std::string filename;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        filename = pending_model_delete_filename_;
        pending_model_delete_filename_.clear();
        model_delete_confirmation_ = false;
        status_ = "Deleting selected model...";
    }
    if (filename.empty()) {
        return;
    }
    StartWorker([this, filename] {
        if (LocalLlamaServerManager::Instance().Snapshot().model_filename == filename) {
            LocalLlamaServerManager::Instance().Unload();
        }
        std::error_code error;
        std::filesystem::remove(ModelsDirectory() / filename, error);
        const bool deleted = !error && !std::filesystem::exists(ModelsDirectory() / filename);
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_reload_ = true;
        pending_status_ = deleted ? "Model deleted." : "Model deletion failed.";
    });
}

void AiSettingsModalContent::CancelSelectedModelDelete() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    pending_model_delete_filename_.clear();
    model_delete_confirmation_ = false;
    status_ = "Model deletion cancelled.";
}

void AiSettingsModalContent::PersistSelectedModel() {
    if (selected_model_ == persisted_model_index_ || selected_model_ < 0 ||
        static_cast<size_t>(selected_model_) >= models_.size() || !config_) {
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
        std::lock_guard<std::mutex> lock(state_mutex_);
        test_popup_visible_ = true;
        test_result_ = "Select a model before running the test.";
        status_ = "AI test cannot start without a selected model.";
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
        test_result_ = "Waiting for model output...";
        status_ = "Running editing test with the selected model...";
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
        AiBackendResult result = AiBackend(settings).Run(
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
    bool stop_test = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        stop_test = test_running_;
        test_popup_visible_ = false;
        if (stop_test) {
            cancel_requested_.store(true);
            operation_state_ = OperationState::Stopping;
            status_ = "Stopping model test because the test window was closed...";
        }
    }
    if (stop_test) {
        command_control_.RequestStop();
    }
    active_panel_ = 0;
    model_menu_->TakeFocus();
    RequestRedraw();
}

void AiSettingsModalContent::StartOrRestartLocalModel() {
    if (selected_model_ < 0 || static_cast<size_t>(selected_model_) >= models_.size()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "Select a local model first.";
        return;
    }
    const AiModelInfo selected = models_[static_cast<size_t>(selected_model_)];
    if (selected.source != AiModelSource::ManagedLocal || !selected.downloaded) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "The selected local model must be downloaded before it can be loaded.";
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        local_model_starting_ = true;
        status_ = "Starting llama-server and loading " + selected.title + "...";
    }
    StartWorker([this, selected] {
        std::string error;
        LocalLlamaModelMetadata metadata;
        metadata.gpu_required = selected.gpu_required;
        metadata.recommended_vram_mb = selected.recommended_vram_mb;
        metadata.text_only = selected.purpose == "text";
        const bool ready = LocalLlamaServerManager::Instance().Restart(
            selected.filename, metadata, 0, 120, error);
        std::lock_guard<std::mutex> lock(state_mutex_);
        local_model_starting_ = false;
        pending_status_ = ready
            ? "Local model loaded and ready."
            : (error.empty() ? "Could not start the local model." : error);
    });
}

void AiSettingsModalContent::CancelCurrentTask() {
    bool should_cancel = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        should_cancel = test_running_ && operation_state_ != OperationState::Stopping;
        if (should_cancel) {
            cancel_requested_.store(true);
            operation_state_ = OperationState::Stopping;
            status_ = "Cancelling current AI task; the model will remain loaded...";
            test_result_ = "Cancelling current AI task...";
        }
    }
    if (should_cancel) {
        command_control_.RequestStop();
        RequestRedraw();
    }
}

void AiSettingsModalContent::UnloadLocalModel() {
    bool cancel_test = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (unload_in_progress_) {
            status_ = "The local model is already being unloaded.";
            return;
        }
        if (busy_ && !test_running_ && !local_model_starting_) {
            status_ = "Finish the current settings operation before unloading the model.";
            return;
        }
        cancel_test = test_running_;
        unload_in_progress_ = true;
        busy_ = true;
        operation_state_ = OperationState::Stopping;
        status_ = "Unloading the local model and stopping llama-server...";
        if (cancel_test) {
            cancel_requested_.store(true);
            test_popup_visible_ = false;
            test_result_ = "Cancelling current AI task...";
        }
    }
    if (cancel_test) {
        command_control_.RequestStop();
        active_panel_ = 0;
        model_menu_->TakeFocus();
    }

    std::thread previous_worker;
    if (worker_.joinable()) {
        previous_worker = std::move(worker_);
    }
    worker_ = std::thread([this, previous = std::move(previous_worker)]() mutable {
        LocalLlamaServerManager::Instance().Unload();
        if (previous.joinable()) {
            previous.join();
        }
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            unload_in_progress_ = false;
            local_model_starting_ = false;
            busy_ = false;
            test_running_ = false;
            operation_state_ = OperationState::Idle;
            status_ = "Local model unloaded; llama-server stopped.";
        }
        RequestRedraw();
    });
    RequestRedraw();
}

void AiSettingsModalContent::PrepareClose() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    test_popup_visible_ = false;
    active_panel_ = 0;
}

bool AiSettingsModalContent::CanStartLocalModel() const {
    if (selected_model_ < 0 || static_cast<size_t>(selected_model_) >= models_.size()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (busy_) {
            return false;
        }
    }
    const AiModelInfo& selected = models_[static_cast<size_t>(selected_model_)];
    return selected.source == AiModelSource::ManagedLocal && selected.downloaded &&
        RuntimeBinaryExists();
}

bool AiSettingsModalContent::CanCancelTask() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return test_running_ && operation_state_ != OperationState::Stopping;
}

bool AiSettingsModalContent::CanUnloadModel() const {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (unload_in_progress_ || operation_state_ == OperationState::Stopping ||
            (busy_ && !test_running_ && !local_model_starting_)) {
            return false;
        }
        if (local_model_starting_) {
            return true;
        }
    }
    const LocalLlamaServerSnapshot snapshot = LocalLlamaServerManager::Instance().Snapshot();
    return snapshot.state != LocalLlamaServerState::Stopped &&
        snapshot.state != LocalLlamaServerState::Stopping;
}

void AiSettingsModalContent::ApplyPendingWorkerResult() {
    bool reload = false;
    bool apply_server_models = false;
    bool connection_success = false;
    AiProvider detected = AiProvider::Auto;
    std::string pending_status;
    std::vector<AiModelInfo> pending_models;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        reload = pending_reload_;
        pending_reload_ = false;
        apply_server_models = pending_server_models_;
        pending_server_models_ = false;
        connection_success = pending_connection_success_;
        detected = pending_detected_provider_;
        pending_status = std::move(pending_status_);
        pending_status_.clear();
        if (apply_server_models) {
            pending_models = std::move(pending_models_);
            pending_models_.clear();
        }
        if (!pending_status.empty()) {
            status_ = pending_status;
        }
    }
    if (apply_server_models) {
        server_models_ = connection_success ? std::move(pending_models) : std::vector<AiModelInfo>{};
        connection_status_ = connection_success
            ? "Connected — " + AiBackend::ProviderLabel(detected) + " detected"
            : (cancel_requested_.load()
                   ? "Connection check stopped"
                   : AiBackend::ProviderLabel(provider_) + " connection failed");
        reload = true;
    }
    if (reload) {
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
            text(" Editing test") | bold | color(theme.modal_accent),
            filler(),
            running ? spinner(0, test_progress_frame_) | bold : text(""),
        }),
        separator() | color(theme.modal_border),
        paragraph(test_source_) | color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        text(" Result") | bold | color(theme.modal_accent),
        paragraph(result.empty() ? "Waiting for model output..." : result) |
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
    bool runtime_confirmation = false;
    bool model_confirmation = false;
    bool test_running = false;
    bool local_model_starting = false;
    bool unload_in_progress = false;
    OperationState operation_state = OperationState::Idle;
    std::string status;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        busy = busy_;
        runtime_confirmation = runtime_delete_confirmation_;
        model_confirmation = model_delete_confirmation_;
        test_running = test_running_;
        local_model_starting = local_model_starting_;
        unload_in_progress = unload_in_progress_;
        operation_state = operation_state_;
        status = status_;
    }
    if (busy) {
        ++operation_progress_frame_;
    }
    const LocalLlamaServerSnapshot local_server =
        LocalLlamaServerManager::Instance().Snapshot();
    std::string local_model_status = "Unloaded";
    if (local_server.state == LocalLlamaServerState::Starting) {
        local_model_status = "Loading " + local_server.model_filename;
    } else if (local_server.state == LocalLlamaServerState::Ready) {
        local_model_status = "Loaded — " + local_server.model_filename;
        if (!local_server.gpu_device.empty()) {
            local_model_status += " · " + local_server.gpu_device;
            if (local_server.gpu_free_memory_mb > 0) {
                local_model_status += " · " +
                    std::to_string(local_server.gpu_free_memory_mb) + " MiB free";
            }
        }
    } else if (local_server.state == LocalLlamaServerState::Stopping) {
        local_model_status = "Unloading";
    } else if (local_server.state == LocalLlamaServerState::Failed) {
        local_model_status = "Failed";
    }

    Elements rows;
    rows.push_back(text(" Connection") | bold | color(theme.modal_text_color));
    rows.push_back(hbox({
        text(" URL: ") | bold | color(theme.modal_accent),
        server_url_input_->Render() |
            borderStyled(LIGHT, theme.modal_border) | flex,
    }));
    rows.push_back(hbox({
        text(" Provider: ") | bold | color(theme.modal_accent),
        provider_ollama_button_->Render(), text(" "),
        provider_openai_button_->Render(), text(" "),
        provider_local_button_->Render(),
    }));
    rows.push_back(separator() | color(theme.modal_border));

    rows.push_back(hbox({
        text(" Models") | bold | color(theme.modal_text_color),
        filler(),
        model_download_button_->Render(), text(" "), model_delete_button_->Render(),
        text(" "), test_button_->Render(),
    }));
    if (model_confirmation) {
        rows.push_back(hbox({
            text(" Delete the selected local model? ") | bold,
            model_confirm_delete_button_->Render(), text(" "),
            model_cancel_delete_button_->Render(),
        }));
    }
    rows.push_back(model_menu_->Render() |
                   borderStyled(LIGHT, theme.modal_border) |
                   size(HEIGHT, EQUAL, 7));
    std::string model_description = "Select a model to view its description.";
    if (selected_model_ >= 0 && static_cast<size_t>(selected_model_) < model_descriptions_.size() &&
        !model_descriptions_[static_cast<size_t>(selected_model_)].empty()) {
        model_description = model_descriptions_[static_cast<size_t>(selected_model_)];
    }
    rows.push_back(paragraph(" " + model_description) |
        dim | color(theme.modal_text_color) | size(HEIGHT, EQUAL, 2));
    rows.push_back(text(
        " [x] active model · server models are managed by the configured AI service") |
        dim | color(theme.modal_text_color));
    rows.push_back(separator() | color(theme.modal_border));

    rows.push_back(hbox({
        text(" Local llama.cpp runtime: ") | bold | color(theme.modal_text_color),
        text(RuntimeBinaryExists() ? "installed" : "not installed") |
            color(theme.modal_accent),
        text("   Path: ") | bold | color(theme.modal_text_color),
        paragraph(RuntimeDisplayPath()) | color(theme.modal_text_color) | flex,
    }));
    rows.push_back(hbox({
        runtime_download_button_->Render(), text(" "), runtime_delete_button_->Render(),
        text(" "), fetch_registry_button_->Render(),
    }));
    if (runtime_confirmation) {
        rows.push_back(hbox({
            text(" Delete the llama.cpp runtime? ") | bold,
            runtime_confirm_delete_button_->Render(), text(" "),
            runtime_cancel_delete_button_->Render(),
        }));
    }
    rows.push_back(separator() | color(theme.modal_border));

    rows.push_back(vbox({
        StatusLine("Connection", connection_status_, theme),
        StatusLine("Local model", local_model_status, theme),
        StatusLine("Task", test_running
            ? (operation_state == OperationState::Stopping ? "Cancelling" : "Editing test")
            : (unload_in_progress
                ? "Unloading model"
                : (local_model_starting ? "Loading model" : "Idle")), theme),
        hbox({
            busy ? spinner(0, operation_progress_frame_) | bold : text(" "),
            text(" Status: ") | bold | color(theme.modal_accent),
            paragraph(status.empty() ? "Ready" : status) |
                color(theme.modal_text_color) | flex,
        }),
    }) | borderStyled(LIGHT, theme.modal_border) | size(HEIGHT, EQUAL, 7));
    return vbox(std::move(rows)) | borderStyled(LIGHT, theme.modal_border);
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
        {"Start / Restart", [this] { content_->StartOrRestartLocalModel(); }, ButtonRole::Secondary,
         [this] { return content_->CanStartLocalModel(); }},
        {"Cancel Task", [this] { content_->CancelCurrentTask(); }, ButtonRole::Warning,
         [this] { return content_->CanCancelTask(); }},
        {"Unload Model", [this] { content_->UnloadLocalModel(); }, ButtonRole::Warning,
         [this] { return content_->CanUnloadModel(); }},
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
