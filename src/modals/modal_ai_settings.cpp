#include "modals/modal_ai_settings.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>

#include <archive.h>
#include <archive_entry.h>

#include "curl_manager.hpp"
#include "ftxui/component/component_options.hpp"
#include "json_utils.hpp"
#include "modals/assistant_modals.hpp"
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
    std::function<bool()> selected = {}) {
    ButtonSpec base = ButtonSpecFromLabel(
        std::move(label),
        ButtonRole::Default,
        ButtonVariant::AccentEdges,
        ButtonSize::Compact);
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

bool RuntimeBinaryExists() {
    std::error_code error;
    const std::filesystem::path directory = RuntimeDirectory();
    if (!std::filesystem::exists(directory, error)) {
        return false;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error)) {
        if (error) {
            return false;
        }
        const std::string filename = entry.path().filename().string();
        if (entry.is_regular_file() && (filename == "llama-cli" || filename == "llama-cli.exe")) {
            return true;
        }
    }
    return false;
}

std::string RuntimeDisplayPath() {
#ifdef _WIN32
    return "%LOCALAPPDATA%\\textlt\\ai\\runtimes\\llama_cpp\\llama-cli.exe";
#else
    return "~/.local/share/textlt/ai/runtimes/llama_cpp/llama-cli";
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

bool DownloadFile(const std::string& url, const std::filesystem::path& final_path) {
    EnsureDirectory(final_path.parent_path());
    const std::filesystem::path part_path = final_path.string() + ".part";
    std::error_code error;
    std::filesystem::remove(part_path, error);
    if (!CurlManager::DownloadToFile(url, part_path)) {
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


bool IsSafeModelFilename(const std::string& filename) {
    if (filename.empty()) {
        return false;
    }
    const std::filesystem::path path(filename);
    return !path.is_absolute() && path.parent_path().empty() &&
        path.filename() == path && filename != "." && filename != "..";
}

std::vector<AiModelInfo> LoadLocalModels(std::vector<std::string>* descriptions) {
    std::vector<AiModelInfo> models;
    Json root;
    if (LoadUserRegistryJson(RegistryKind::Ai, &root) != RegistryLoadResult::Loaded) {
        return models;
    }
    const auto items = root.find("models");
    if (items == root.end() || !items->is_array()) {
        return models;
    }
    for (const Json& item : *items) {
        if (!item.is_object()) {
            continue;
        }
        const std::string filename = JsonString(item, "filename");
        if (!IsSafeModelFilename(filename)) {
            continue;
        }
        std::error_code error;
        const bool downloaded = std::filesystem::exists(ModelsDirectory() / filename, error);
        AiModelInfo model;
        model.key = "local:" + filename;
        model.id = filename;
        model.title = JsonLabel(item, "title", "id");
        model.provider_label = JsonString(item, "backend", "llama.cpp");
        model.filename = filename;
        model.source = AiModelSource::ManagedLocal;
        model.available = downloaded && RuntimeBinaryExists();
        model.downloaded = downloaded;
        models.push_back(std::move(model));
        if (descriptions) {
            descriptions->push_back(JsonString(item, "description"));
        }
    }
    return models;
}

bool FindRegistryModel(const std::string& filename, Json* selected) {
    Json root;
    if (LoadUserRegistryJson(RegistryKind::Ai, &root) != RegistryLoadResult::Loaded) {
        return false;
    }
    const auto models = root.find("models");
    if (models == root.end() || !models->is_array()) {
        return false;
    }
    for (const Json& model : *models) {
        if (model.is_object() && IsSafeModelFilename(filename) &&
            JsonString(model, "filename") == filename) {
            if (selected) {
                *selected = model;
            }
            return true;
        }
    }
    return false;
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

    provider_auto_button_ = MakeButton(
        &theme_, "Auto", [this] { SelectProvider(AiProvider::Auto); },
        [this] { return provider_ == AiProvider::Auto; });
    provider_ollama_button_ = MakeButton(
        &theme_, "Ollama", [this] { SelectProvider(AiProvider::Ollama); },
        [this] { return provider_ == AiProvider::Ollama; });
    provider_openai_button_ = MakeButton(
        &theme_, "OpenAI-compatible", [this] { SelectProvider(AiProvider::OpenAiCompatible); },
        [this] { return provider_ == AiProvider::OpenAiCompatible; });
    provider_local_button_ = MakeButton(
        &theme_, "Local llama.cpp", [this] { SelectProvider(AiProvider::LocalLlamaCpp); },
        [this] { return provider_ == AiProvider::LocalLlamaCpp; });
    save_connection_button_ = MakeButton(
        &theme_, "Save", [this] { SaveConnectionSettings(); });
    connect_button_ = MakeButton(
        &theme_, "Connect / refresh", [this] { ConnectAndRefreshServerModels(); });
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

    container_ = ftxui::Container::Vertical({
        server_url_input_,
        ftxui::Container::Horizontal({
            provider_auto_button_, provider_ollama_button_,
            provider_openai_button_, provider_local_button_,
        }),
        ftxui::Container::Horizontal({save_connection_button_, connect_button_}),
        ftxui::Container::Horizontal({
            runtime_download_button_, runtime_delete_button_, fetch_registry_button_,
            model_download_button_, model_delete_button_,
        }),
        ftxui::Container::Horizontal({
            runtime_confirm_delete_button_, runtime_cancel_delete_button_,
            model_confirm_delete_button_, model_cancel_delete_button_,
        }),
        model_menu_,
    });
    RefreshFromConfig();
}

AiSettingsModalContent::~AiSettingsModalContent() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void AiSettingsModalContent::RefreshFromConfig() {
    if (config_) {
        server_url_ = config_->ai_server_url;
        provider_ = AiBackend::ProviderFromConfig(config_->ai_provider);
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
        } else if (selected_key.rfind("local:", 0) == 0 &&
                   IsSafeModelFilename(placeholder.id)) {
            placeholder.provider_label = "llama.cpp";
            placeholder.filename = placeholder.id;
            placeholder.source = AiModelSource::ManagedLocal;
            std::error_code error;
            placeholder.downloaded = std::filesystem::exists(
                ModelsDirectory() / placeholder.filename, error);
            placeholder.available = placeholder.downloaded && RuntimeBinaryExists();
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
        model_labels_.push_back(
            model.title + " | " + model.provider_label + " | " + ModelStatus(model));
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
    if (config_) {
        config_->ai_server_url = server_url_;
        config_->ai_provider = AiBackend::ProviderToConfig(provider_);
        if (!config_->Persist()) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            status_ = "Could not save AI settings.";
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    status_ = "AI connection settings saved.";
    return true;
}

void AiSettingsModalContent::SelectProvider(AiProvider provider) {
    provider_ = provider;
    SaveConnectionSettings();
}

void AiSettingsModalContent::ConnectAndRefreshServerModels() {
    if (!SaveConnectionSettings()) {
        return;
    }
    const AiBackendSettings settings{
        server_url_, provider_, config_ ? config_->ai_selected_model_key : std::string{}, 30};
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = provider_ == AiProvider::LocalLlamaCpp
            ? "Checking local llama.cpp runtime..."
            : "Connecting to AI server...";
    }
    StartWorker([this, settings] {
        const AiConnectionResult result = AiBackend(settings).CheckConnectionAndListModels();
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_server_models_ = true;
        pending_connection_success_ = result.success;
        pending_detected_provider_ = result.provider;
        if (!result.success) {
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
            RegistryFilename(RegistryKind::Ai));
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_reload_ = result == assistant_modal_detail::RegistryDownloadResult::Saved;
        pending_status_ = pending_reload_
            ? "AI registry loaded."
            : "AI registry download failed.";
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
        const bool downloaded = DownloadFile(url, archive_path);
        bool installed = false;
        if (downloaded) {
            std::error_code error;
            std::filesystem::remove_all(RuntimeDirectory(), error);
            installed = ExtractArchive(archive_path, RuntimeDirectory()) && RuntimeBinaryExists();
            std::filesystem::remove(archive_path, error);
        }
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_reload_ = true;
        pending_status_ = installed ? "llama.cpp runtime installed." : "Runtime installation failed.";
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
    Json registry_model;
    if (!FindRegistryModel(selected.filename, &registry_model)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_ = "Selected model is not present in the registry.";
        return;
    }
    const std::string url = JsonString(registry_model, "model_url");
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
        const bool downloaded = DownloadFile(url, ModelsDirectory() / selected.filename);
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_reload_ = true;
        pending_status_ = downloaded ? "Model downloaded." : "Model download failed.";
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
        detected_provider_ = connection_success
            ? AiBackend::ProviderLabel(detected)
            : "Not connected";
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

std::string AiSettingsModalContent::GetFooterText() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return status_.size() <= 92 ? status_ : status_.substr(0, 92);
}

ftxui::Element AiSettingsModalContent::Render() {
    ApplyPendingWorkerResult();
    PersistSelectedModel();
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return RenderContent(theme) |
        ftxui::bgcolor(theme.modal_input_bg) |
        ftxui::color(theme.modal_input_fg);
}

ftxui::Element AiSettingsModalContent::RenderContent(const Theme& theme) {
    using namespace ftxui;
    bool busy = false;
    bool runtime_confirmation = false;
    bool model_confirmation = false;
    std::string status;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        busy = busy_;
        runtime_confirmation = runtime_delete_confirmation_;
        model_confirmation = model_delete_confirmation_;
        status = status_;
    }

    Elements rows;
    rows.push_back(text(" Connection") | bold | color(theme.modal_text_color));
    rows.push_back(hbox({
        text(" URL: ") | bold | color(theme.modal_accent),
        server_url_input_->Render() | flex,
    }));
    rows.push_back(hbox({
        text(" Provider: ") | bold | color(theme.modal_accent),
        provider_auto_button_->Render(), text(" "),
        provider_ollama_button_->Render(), text(" "),
        provider_openai_button_->Render(), text(" "),
        provider_local_button_->Render(),
    }));
    rows.push_back(hbox({
        save_connection_button_->Render(), text(" "), connect_button_->Render(),
    }));
    rows.push_back(StatusLine("detected", detected_provider_, theme));
    rows.push_back(separator() | color(theme.modal_border));

    rows.push_back(text(" Managed local runtime") | bold | color(theme.modal_text_color));
    rows.push_back(StatusLine(
        "llama.cpp", RuntimeBinaryExists() ? "installed" : "not installed", theme));
    rows.push_back(StatusLine("path", RuntimeDisplayPath(), theme));
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

    rows.push_back(hbox({
        text(" Models") | bold | color(theme.modal_text_color),
        filler(),
        model_download_button_->Render(), text(" "), model_delete_button_->Render(),
    }));
    if (model_confirmation) {
        rows.push_back(hbox({
            text(" Delete the selected local model? ") | bold,
            model_confirm_delete_button_->Render(), text(" "),
            model_cancel_delete_button_->Render(),
        }));
    }
    rows.push_back(model_menu_->Render() |
                   size(HEIGHT, LESS_THAN, 9) |
                   borderStyled(LIGHT, theme.modal_border));
    if (selected_model_ >= 0 && static_cast<size_t>(selected_model_) < model_descriptions_.size() &&
        !model_descriptions_[static_cast<size_t>(selected_model_)].empty()) {
        rows.push_back(paragraph(
            " " + model_descriptions_[static_cast<size_t>(selected_model_)]) |
            dim | color(theme.modal_text_color));
    }
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(StatusLine("status", busy ? status + " (working)" : status, theme));
    rows.push_back(text(
        " [x] is the active model. Server models are managed by Ollama or the configured API.") |
        dim | color(theme.modal_text_color));
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
    modal_->SetFooterButtons({{"Close", [this] { Close(); }}});
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
    open_ = false;
}

bool AiSettingsModal::IsOpen() const {
    return open_;
}

bool AiSettingsModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}

} // namespace textlt
