#include "assistant_modals.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <utility>

#include <archive.h>
#include <archive_entry.h>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

#include "piper_manager.hpp"
#include "ui_button.hpp"

namespace textlt {
namespace assistant_modal_detail {

std::filesystem::path UserHomeDirectory() {
#ifdef _WIN32
    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && !std::string(user_profile).empty()) {
        return std::filesystem::path(user_profile);
    }
    return {};
#else
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return {};
    }
    return std::filesystem::path(home);
#endif
}

std::filesystem::path UserDataDirectory() {
#ifdef _WIN32
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data && !std::string(local_app_data).empty()) {
        return std::filesystem::path(local_app_data) / "textlt";
    }
    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / "AppData" / "Local" / "textlt";
#else
    const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home && !std::string(xdg_data_home).empty()) {
        return std::filesystem::path(xdg_data_home) / "textlt";
    }
    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / ".local" / "share" / "textlt";
#endif
}

std::filesystem::path DownloadCacheDirectory() {
#ifdef _WIN32
    const std::filesystem::path data = UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "cache" / "downloads";
#else
    const char* xdg_cache_home = std::getenv("XDG_CACHE_HOME");
    if (xdg_cache_home && !std::string(xdg_cache_home).empty()) {
        return std::filesystem::path(xdg_cache_home) / "textlt" / "downloads";
    }
    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / ".cache" / "textlt" / "downloads";
#endif
}

std::filesystem::path RegistryDirectory() {
    const std::filesystem::path data = UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "registries";
}

void EnsureDirectory(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(path, error);
}

RegistryLoadResult LoadUserRegistryJson(const char* filename, Json* root) {
    const std::filesystem::path user_path = RegistryDirectory() / filename;
    std::error_code error;
    if (!std::filesystem::exists(user_path, error)) {
        *root = Json::object();
        return RegistryLoadResult::Missing;
    }

    std::ifstream file(user_path, std::ios::binary);
    if (!file) {
        *root = Json::object();
        return RegistryLoadResult::ParseFailed;
    }

    Json parsed = Json::parse(file, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        *root = Json::object();
        return RegistryLoadResult::ParseFailed;
    }

    *root = std::move(parsed);
    return RegistryLoadResult::Loaded;
}

const char* RegistryFilename(RegistryKind kind) {
    return kind == RegistryKind::Piper ? kPiperRegistryFile : kAiRegistryFile;
}

RegistryLoadResult LoadUserRegistryJson(RegistryKind kind, Json* root) {
    return LoadUserRegistryJson(RegistryFilename(kind), root);
}

std::string JsonLabel(const Json& object, const char* primary, const char* fallback) {
    const std::string value = JsonString(object, primary);
    return value.empty() ? JsonString(object, fallback) : value;
}

std::string BracketLabel(const std::string& label) {
    return !label.empty() && label.front() == '[' ? label : "[" + label + "]";
}

namespace {

bool IsValidJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    const Json parsed = Json::parse(file, nullptr, false);
    return !parsed.is_discarded() && parsed.is_object();
}

std::string EscapeRawNewlinesInJsonStrings(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    bool in_string = false;
    bool escaped = false;
    for (char character : input) {
        if (escaped) {
            output.push_back(character);
            escaped = false;
            continue;
        }
        if (character == '\\' && in_string) {
            output.push_back(character);
            escaped = true;
            continue;
        }
        if (character == '"') {
            in_string = !in_string;
            output.push_back(character);
            continue;
        }
        if (in_string && character == '\n') {
            output += "\\n";
            continue;
        }
        if (in_string && character == '\r') {
            continue;
        }
        output.push_back(character);
    }
    return output;
}

bool NormalizeJsonFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    const std::string content(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    const std::string normalized = EscapeRawNewlinesInJsonStrings(content);
    const Json parsed = Json::parse(normalized, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << normalized;
    return static_cast<bool>(output);
}

bool ReplaceFileFromFile(const std::filesystem::path& source,
                         const std::filesystem::path& destination) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return false;
    }
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << input.rdbuf();
    return static_cast<bool>(output);
}

std::string CurrentRuntimePlatform() {
#ifdef _WIN32
    return "windows";
#else
    return "linux";
#endif
}

std::string CurrentRuntimeArch() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x64";
#else
    return {};
#endif
}

std::string CurrentRuntimeKey() {
    const std::string platform = CurrentRuntimePlatform();
    const std::string arch = CurrentRuntimeArch();
    if (platform.empty() || arch.empty()) {
        return {};
    }
    return platform + "_" + arch;
}

std::string AiRuntimeDownloadUrl() {
    using namespace assistant_modal_detail;

    Json root;
    if (LoadUserRegistryJson(RegistryKind::Ai, &root) != RegistryLoadResult::Loaded) {
        return {};
    }

    const std::string backend_id = JsonString(root, "default_backend", "llama_cpp");
    const std::string runtime_key = CurrentRuntimeKey();
    if (runtime_key.empty()) {
        return {};
    }

    const auto backends = root.find("backends");
    if (backends == root.end() || !backends->is_array()) {
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

bool FindSelectedAiModel(int selected_model, Json* selected) {
    using namespace assistant_modal_detail;

    Json root;
    if (LoadUserRegistryJson(RegistryKind::Ai, &root) != RegistryLoadResult::Loaded) {
        return false;
    }

    const auto models = root.find("models");
    if (models == root.end() || !models->is_array()) {
        return false;
    }

    int visible_index = 0;
    for (const Json& model : *models) {
        if (!model.is_object()) {
            continue;
        }
        if (visible_index == selected_model) {
            *selected = model;
            return true;
        }
        ++visible_index;
    }
    return false;
}

struct RuntimeDownloadContext {
    std::mutex* mutex = nullptr;
    std::atomic<float>* progress = nullptr;
    unsigned long long* downloaded_bytes = nullptr;
    unsigned long long* total_bytes = nullptr;
    std::function<void()>* request_redraw = nullptr;
};

bool UpdateRuntimeProgress(RuntimeDownloadContext* context,
                           unsigned long long total,
                           unsigned long long downloaded) {
    if (!context || !context->mutex || !context->progress) {
        return true;
    }
    {
        std::lock_guard<std::mutex> lock(*context->mutex);
        if (context->downloaded_bytes) {
            *context->downloaded_bytes =
                downloaded > 0 ? static_cast<unsigned long long>(downloaded) : 0;
        }
        if (context->total_bytes && total > 0) {
            *context->total_bytes = static_cast<unsigned long long>(total);
        }
        context->progress->store(total > 0
            ? static_cast<float>(downloaded) / static_cast<float>(total)
            : (downloaded > 0 ? 0.05f : 0.0f));
    }
    if (context->request_redraw && *context->request_redraw) {
        (*context->request_redraw)();
    }
    return true;
}

bool DownloadRuntimeArchive(const std::string& url,
                            const std::filesystem::path& final_path,
                            std::mutex& state_mutex,
                            std::atomic<float>& progress,
                            unsigned long long& downloaded_bytes,
                            unsigned long long& total_bytes,
                            std::function<void()>& request_redraw) {
    using namespace assistant_modal_detail;

    EnsureDirectory(final_path.parent_path());
    const std::filesystem::path part_path = final_path.string() + ".part";
    std::error_code error;
    std::filesystem::remove(part_path, error);

    RuntimeDownloadContext context{
        &state_mutex,
        &progress,
        &downloaded_bytes,
        &total_bytes,
        &request_redraw,
    };
    const bool download_ok = CurlManager::DownloadToFile(
        url,
        part_path,
        [&context](unsigned long long total, unsigned long long downloaded) {
            return UpdateRuntimeProgress(&context, total, downloaded);
        });

    if (!download_ok) {
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

bool DownloadAiModelFile(const std::string& url,
                         const std::filesystem::path& final_path,
                         const std::filesystem::path& part_path,
                         std::mutex& state_mutex,
                         std::atomic<float>& progress,
                         unsigned long long& downloaded_bytes,
                         unsigned long long& total_bytes,
                         std::function<void()>& request_redraw) {
    using namespace assistant_modal_detail;

    EnsureDirectory(final_path.parent_path());
    EnsureDirectory(part_path.parent_path());
    std::error_code error;
    std::filesystem::remove(part_path, error);

    RuntimeDownloadContext context{
        &state_mutex,
        &progress,
        &downloaded_bytes,
        &total_bytes,
        &request_redraw,
    };
    const bool download_ok = CurlManager::DownloadToFile(
        url,
        part_path,
        [&context](unsigned long long total, unsigned long long downloaded) {
            return UpdateRuntimeProgress(&context, total, downloaded);
        });

    if (!download_ok) {
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
        if (result != ARCHIVE_OK) {
            return false;
        }
        if (archive_write_data_block(output, buffer, size, offset) != ARCHIVE_OK) {
            return false;
        }
    }
}

bool ExtractRuntimeArchive(const std::filesystem::path& archive_path,
                           const std::filesystem::path& destination) {
    using namespace assistant_modal_detail;

    EnsureDirectory(destination);

    archive* input = archive_read_new();
    archive* output = archive_write_disk_new();
    if (!input || !output) {
        if (input) {
            archive_read_free(input);
        }
        if (output) {
            archive_write_free(output);
        }
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

    archive_entry* entry = nullptr;
    bool ok = true;
    while (archive_read_next_header(input, &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname(entry);
        const std::filesystem::path relative_path = pathname ? pathname : "";
        if (!IsSafeArchivePath(relative_path)) {
            archive_read_data_skip(input);
            continue;
        }

        const std::filesystem::path full_path = destination / relative_path;
        const std::string full_path_string = full_path.string();
        archive_entry_set_pathname(entry, full_path_string.c_str());

        const int header_result = archive_write_header(output, entry);
        if (header_result != ARCHIVE_OK && header_result != ARCHIVE_WARN) {
            ok = false;
            break;
        }
        if (archive_entry_size(entry) > 0 && !CopyArchiveData(input, output)) {
            ok = false;
            break;
        }
        if (archive_write_finish_entry(output) != ARCHIVE_OK) {
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

bool RuntimeBinaryExists(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        return false;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error)) {
        if (error) {
            return false;
        }
        const std::string filename = entry.path().filename().string();
        if (filename == "llama-cli" || filename == "llama-cli.exe") {
            return true;
        }
    }
    return false;
}

} // namespace

RegistryDownloadResult DownloadRegistry(const char* url, const char* filename) {
    const std::filesystem::path registry_directory = RegistryDirectory();
    if (registry_directory.empty()) {
        return RegistryDownloadResult::Failed;
    }

    EnsureDirectory(registry_directory);

    const std::filesystem::path final_path = registry_directory / filename;
    CurlManager::RequestOptions options;
    options.no_cache = true;
    options.fresh_connect = true;
    const CurlManager::Response response =
        CurlManager::Get(CurlManager::WithCacheBust(url), options);
    if (!response.ok) {
        return RegistryDownloadResult::Failed;
    }

    if (response.body.empty()) {
        return RegistryDownloadResult::Empty;
    }

    std::string body = response.body;
    Json parsed = Json::parse(body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        body = EscapeRawNewlinesInJsonStrings(body);
        parsed = Json::parse(body, nullptr, false);
    }
    if (parsed.is_discarded() || !parsed.is_object()) {
        return RegistryDownloadResult::InvalidJson;
    }

    if (!WriteJsonAtomically(final_path, parsed)) {
        return RegistryDownloadResult::Failed;
    }

    return RegistryDownloadResult::Saved;
}

} // namespace assistant_modal_detail

AssistantSettingsModalContent::AssistantSettingsModalContent(
    const Theme* theme,
    std::function<void()> request_redraw)
    : theme_(theme),
      request_redraw_(std::move(request_redraw)) {
    auto make_button = [this](std::string label,
                              std::function<void()> on_click,
                              ButtonRole role = ButtonRole::Default) {
        ButtonSpec spec = ButtonSpecFromLabel(std::move(label), role, ButtonVariant::AccentEdges);
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        option.label = ButtonCaptionText(spec);
        option.on_click = std::move(on_click);
        option.transform = [this, spec = std::move(spec)](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            return RenderButton(theme, spec, state.focused || state.active);
        };
        return ftxui::Button(option);
    };
    auto make_tab_button = [this](std::string label, int tab_index) {
        ButtonSpec spec = ButtonSpecFromLabel(std::move(label), ButtonRole::Tab, ButtonVariant::AccentEdges, ButtonSize::Compact);
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        option.label = ButtonCaptionText(spec);
        option.on_click = [this, tab_index] { selected_tab_ = tab_index; };
        option.transform = [this, tab_index, spec = std::move(spec)](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            ButtonSpec resolved_spec = spec;
            resolved_spec.selected = selected_tab_ == tab_index;
            ftxui::Element tab = RenderButton(theme, resolved_spec, state.focused || state.active);
            return selected_tab_ == tab_index ? tab | ftxui::bold : tab | ftxui::dim;
        };
        return ftxui::Button(option);
    };

    tts_tab_button_ = make_tab_button("TTS", 0);
    ai_tab_button_ = make_tab_button("AI", 1);
    tab_buttons_ = ftxui::Container::Horizontal({
        tts_tab_button_,
        ai_tab_button_,
    });

    ftxui::MenuOption language_option = ftxui::MenuOption::Vertical();
    language_option.on_change = [this] { RebuildTtsVoices(); };
    tts_language_menu_ =
        ftxui::Menu(&tts_language_labels_, &selected_tts_language_, language_option);

    ftxui::MenuOption checkbox_option = ftxui::MenuOption::Vertical();
    checkbox_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(std::string(state.active ? "[x] " : "[ ] ") + state.label);
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return row | ftxui::color(theme.modal_text_color);
    };
    tts_voice_menu_ = ftxui::Menu(&tts_voice_labels_, &selected_tts_voice_, checkbox_option);
    ai_model_menu_ = ftxui::Menu(&ai_model_labels_, &selected_ai_model_, checkbox_option);

    fetch_tts_button_ = make_button("Fetch registry", [this] { FetchRegistries(); }, ButtonRole::Primary);
    tts_runtime_install_button_ =
        make_button("Install Piper", [this] { StartPiperRuntimeInstall(); });
    tts_download_button_ = make_button("Download voice", [this] { StartTtsVoiceDownload(); });
    tts_delete_button_ = make_button("Delete", [this] { StartTtsVoiceDelete(); });
    tts_confirm_delete_button_ =
        make_button("Confirm delete", [this] { ConfirmTtsVoiceDelete(); });
    tts_cancel_delete_button_ =
        make_button("Cancel", [this] { CancelTtsVoiceDelete(); });
    tts_test_button_ = make_button("Test", [this] { TestTtsVoice(); }, ButtonRole::Primary);
    tts_test_popup_close_button_ =
        make_button("Close", [this] { CloseTtsTestPopup(); });
    fetch_ai_button_ = make_button("Fetch registry", [this] { FetchRegistries(); }, ButtonRole::Primary);
    ai_runtime_download_button_ =
        make_button("Download AI runtime", [this] { StartAiRuntimeDownload(); });
    ai_runtime_delete_button_ =
        make_button("Delete runtime", [this] { StartAiRuntimeDelete(); });
    ai_runtime_confirm_delete_button_ =
        make_button("Confirm delete", [this] { ConfirmAiRuntimeDelete(); });
    ai_runtime_cancel_delete_button_ =
        make_button("Cancel", [this] { CancelAiRuntimeDelete(); });
    ai_model_download_button_ =
        make_button("Download model", [this] { StartAiModelDownload(); });
    ai_delete_model_button_ =
        make_button("Delete model", [this] { StartAiModelDelete(); });
    ai_model_confirm_delete_button_ =
        make_button("Confirm delete", [this] { ConfirmAiModelDelete(); });
    ai_model_cancel_delete_button_ =
        make_button("Cancel", [this] { CancelAiModelDelete(); });

    tab_body_container_ = ftxui::Container::Tab({
        ftxui::Container::Vertical({
            ftxui::Container::Horizontal({
                fetch_tts_button_,
                tts_runtime_install_button_,
                tts_download_button_,
                tts_delete_button_,
                tts_test_button_,
            }),
            tts_language_menu_,
            tts_voice_menu_,
            ftxui::Container::Horizontal({
                tts_confirm_delete_button_,
                tts_cancel_delete_button_,
            }),
        }),
        ftxui::Container::Vertical({
            ftxui::Container::Horizontal({
                fetch_ai_button_,
                ai_runtime_download_button_,
                ai_runtime_delete_button_,
                ai_runtime_confirm_delete_button_,
                ai_runtime_cancel_delete_button_,
                ai_model_download_button_,
                ai_delete_model_button_,
                ai_model_confirm_delete_button_,
                ai_model_cancel_delete_button_,
            }),
            ai_model_menu_,
        }),
    }, &selected_tab_);

    auto primary_controls = ftxui::Container::Vertical({
        tab_buttons_,
        tab_body_container_,
    });
    auto popup_controls = ftxui::CatchEvent(tts_test_popup_close_button_, [this](ftxui::Event event) {
        if (event == ftxui::Event::Escape) {
            CloseTtsTestPopup();
            return true;
        }
        return false;
    });
    container_ = ftxui::Container::Tab({primary_controls, popup_controls}, &popup_layer_index_);

    LoadRegistries();
}

AssistantSettingsModalContent::~AssistantSettingsModalContent() {
    tts_cancel_download_ = true;
    if (tts_download_thread_.joinable()) {
        tts_download_thread_.join();
    }
    if (tts_runtime_thread_.joinable()) {
        tts_runtime_thread_.join();
    }
    if (ai_runtime_thread_.joinable()) {
        ai_runtime_thread_.join();
    }
    if (ai_model_thread_.joinable()) {
        ai_model_thread_.join();
    }
    if (fetch_thread_.joinable()) {
        fetch_thread_.join();
    }
}

ftxui::Element AssistantSettingsModalContent::Render() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return (selected_tab_ == 0 ? RenderTtsTab(theme) : RenderAiTab(theme)) |
        ftxui::bgcolor(theme.modal_input_bg) |
        ftxui::color(theme.modal_input_fg);
}

ftxui::Element AssistantSettingsModalContent::RenderTitle() {
    using namespace ftxui;
    return hbox({
        tts_tab_button_->Render(),
        text(" "),
        ai_tab_button_->Render(),
    });
}

std::string AssistantSettingsModalContent::GetFooterText() const {
    if (selected_tab_ == 0) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        return tts_status_;
    }
    std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
    return ai_status_;
}

void AssistantSettingsModalContent::LoadRegistries() {
    LoadPiperRegistry();
    LoadAiRegistry();
}

void AssistantSettingsModalContent::FetchRegistries() {
    using namespace assistant_modal_detail;

    {
        std::lock_guard<std::mutex> tts_lock(tts_download_mutex_);
        if (tts_downloading_) {
            tts_status_ = "Voice download is running";
            return;
        }
    }
    {
        std::lock_guard<std::mutex> ai_lock(ai_runtime_mutex_);
        if (ai_runtime_downloading_ || ai_model_downloading_ || ai_model_deleting_) {
            return;
        }
    }
    if (fetch_thread_.joinable()) {
        fetch_thread_.join();
    }

    {
        std::lock_guard<std::mutex> tts_lock(tts_download_mutex_);
        tts_status_ = "Fetching TTS registry...";
        tts_progress_ = 0.0f;
    }
    {
        std::lock_guard<std::mutex> ai_lock(ai_runtime_mutex_);
        ai_status_ = "Fetching AI registry...";
        ai_progress_ = 0.0f;
    }
    RequestRedraw();

    fetch_thread_ = std::thread([this] {
        const RegistryDownloadResult piper_download_result =
            DownloadRegistry(CurlManager::kPiperRegistryUrl, RegistryFilename(RegistryKind::Piper));
        const RegistryDownloadResult ai_download_result =
            DownloadRegistry(CurlManager::kAiRegistryUrl, RegistryFilename(RegistryKind::Ai));

        LoadPiperRegistry();
        LoadAiRegistry();

        auto registry_status = [](const std::string& label,
                                  RegistryDownloadResult download_result,
                                  RegistryLoadResult load_result) {
            if (download_result == RegistryDownloadResult::Saved &&
                load_result == RegistryLoadResult::Loaded) {
                return label + " registry loaded";
            }
            if (download_result == RegistryDownloadResult::Empty) {
                return label + " registry file is empty";
            }
            if (download_result == RegistryDownloadResult::InvalidJson) {
                return "Downloaded " + label + " registry is invalid JSON";
            }
            if (load_result == RegistryLoadResult::Missing) {
                return label + " registry not loaded";
            }
            if (load_result == RegistryLoadResult::ParseFailed) {
                return label + " registry parse failed";
            }
            return label + " registry download failed";
        };

        Json piper_root;
        Json ai_root;
        const RegistryLoadResult piper_load_result =
            LoadUserRegistryJson(RegistryKind::Piper, &piper_root);
        const RegistryLoadResult ai_load_result =
            LoadUserRegistryJson(RegistryKind::Ai, &ai_root);

        {
            std::lock_guard<std::mutex> lock(tts_download_mutex_);
            tts_status_ = registry_status("TTS", piper_download_result, piper_load_result);
        }
        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_status_ = registry_status("AI", ai_download_result, ai_load_result);
        }
        RequestRedraw();
    });
}

void AssistantSettingsModalContent::SetTodoStatus(std::string action) {
    using namespace assistant_modal_detail;

    if (selected_tab_ == 0) {
        EnsureDirectory(PiperManager::ModelsDirectory());
        EnsureDirectory(DownloadCacheDirectory());
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "TODO: " + action + " support is not implemented";
        tts_progress_ = 0.0f;
        return;
    }
    EnsureDirectory(UserDataDirectory() / "ai" / "models");
    EnsureDirectory(UserDataDirectory() / "ai" / "runtimes");
    EnsureDirectory(DownloadCacheDirectory());
    std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
    ai_status_ = "TODO: " + action + " support is not implemented";
    ai_runtime_progress_visible_ = false;
    ai_runtime_extracting_ = false;
    ai_progress_ = 0.0f;
}

bool AssistantSettingsModalContent::ResolveAiRuntimeDownload() {
    using namespace assistant_modal_detail;

    EnsureDirectory(UserDataDirectory() / "ai" / "runtimes");
    EnsureDirectory(DownloadCacheDirectory());

    ai_runtime_download_url_.clear();
    ai_runtime_asset_name_.clear();
    ai_runtime_progress_visible_ = false;
    ai_runtime_extracting_ = false;
    ai_progress_ = 0.0f;
    ai_status_ = "Loading runtime registry...";

    const std::string url = AiRuntimeDownloadUrl();
    if (url.empty()) {
        ai_status_ = "Runtime asset not found";
        return false;
    }

    ai_runtime_download_url_ = url;
    ai_runtime_asset_name_ = std::filesystem::path(url).filename().string();
    ai_status_ = "Runtime asset found: " + ai_runtime_asset_name_;
    return true;
}

void AssistantSettingsModalContent::StartAiRuntimeDownload() {
    using namespace assistant_modal_detail;

    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        if (ai_runtime_downloading_ || ai_model_downloading_ || ai_model_deleting_) {
            return;
        }
    }
    if (ai_runtime_thread_.joinable()) {
        ai_runtime_thread_.join();
    }

    if (ai_runtime_download_url_.empty() && !ResolveAiRuntimeDownload()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_runtime_downloading_ = true;
        ai_runtime_progress_visible_ = true;
        ai_runtime_extracting_ = false;
        ai_progress_ = 0.0f;
        ai_runtime_downloaded_bytes_ = 0;
        ai_runtime_total_bytes_ = 0;
        ai_status_ = "Runtime downloading...";
    }
    RequestRedraw();

    const std::string url = ai_runtime_download_url_;
    const std::string asset_name = ai_runtime_asset_name_.empty()
        ? std::filesystem::path(url).filename().string()
        : ai_runtime_asset_name_;
    const std::filesystem::path archive_path = DownloadCacheDirectory() / asset_name;
    const std::filesystem::path runtime_directory =
        UserDataDirectory() / "ai" / "runtimes" / "llama_cpp";

    ai_runtime_thread_ = std::thread([this, url, archive_path, runtime_directory] {
        const bool download_ok = DownloadRuntimeArchive(
            url,
            archive_path,
            ai_runtime_mutex_,
            ai_progress_,
            ai_runtime_downloaded_bytes_,
            ai_runtime_total_bytes_,
            request_redraw_);
        if (!download_ok) {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_runtime_downloading_ = false;
            ai_runtime_progress_visible_ = false;
            ai_runtime_extracting_ = false;
            ai_progress_ = 0.0f;
            ai_runtime_downloaded_bytes_ = 0;
            ai_runtime_total_bytes_ = 0;
            ai_status_ = "Runtime install failed";
            RequestRedraw();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_status_ = "Runtime extracting...";
            ai_runtime_progress_visible_ = true;
            ai_runtime_extracting_ = true;
            ai_progress_ = 0.0f;
            ai_runtime_downloaded_bytes_ = 0;
            ai_runtime_total_bytes_ = 0;
        }
        RequestRedraw();

        const bool extract_ok = ExtractRuntimeArchive(archive_path, runtime_directory);
        for (int step = 1; step <= 200; ++step) {
            {
                std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
                ai_progress_ = static_cast<float>(step) / 200.0f;
            }
            RequestRedraw();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        const bool binary_ok = extract_ok && RuntimeBinaryExists(runtime_directory);
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_runtime_downloading_ = false;
        ai_runtime_progress_visible_ = false;
        ai_runtime_extracting_ = false;
        ai_progress_ = 0.0f;
        ai_runtime_downloaded_bytes_ = 0;
        ai_runtime_total_bytes_ = 0;
        ai_status_ = binary_ok ? "Runtime installed" : "Runtime install failed";
        RequestRedraw();
    });
}

void AssistantSettingsModalContent::StartAiRuntimeDelete() {
    using namespace assistant_modal_detail;

    const std::filesystem::path runtime_directory =
        UserDataDirectory() / "ai" / "runtimes" / "llama_cpp";
    if (!RuntimeBinaryExists(runtime_directory)) {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_runtime_delete_confirm_visible_ = false;
        ai_status_ = "Runtime not installed";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        if (ai_model_downloading_ || ai_model_deleting_) {
            return;
        }
        ai_runtime_delete_confirm_visible_ = true;
    }
    if (ai_runtime_confirm_delete_button_) {
        ai_runtime_confirm_delete_button_->TakeFocus();
    }
}

void AssistantSettingsModalContent::ConfirmAiRuntimeDelete() {
    using namespace assistant_modal_detail;

    const std::filesystem::path runtime_directory =
        UserDataDirectory() / "ai" / "runtimes" / "llama_cpp";
    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        if (ai_runtime_downloading_ || ai_model_downloading_ || ai_model_deleting_) {
            return;
        }
    }
    if (ai_runtime_thread_.joinable()) {
        ai_runtime_thread_.join();
    }

    if (!RuntimeBinaryExists(runtime_directory)) {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_runtime_delete_confirm_visible_ = false;
        ai_status_ = "Runtime not installed";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_runtime_downloading_ = true;
        ai_runtime_delete_confirm_visible_ = false;
        ai_runtime_progress_visible_ = true;
        ai_runtime_extracting_ = true;
        ai_status_ = "Runtime deleting...";
        ai_progress_ = 0.0f;
        ai_runtime_downloaded_bytes_ = 0;
        ai_runtime_total_bytes_ = 0;
    }
    RequestRedraw();

    ai_runtime_thread_ = std::thread([this, runtime_directory] {
        for (int step = 1; step <= 70; ++step) {
            {
                std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
                ai_progress_ = static_cast<float>(step) / 70.0f;
            }
            RequestRedraw();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::error_code error;
        std::filesystem::remove_all(runtime_directory, error);
        const bool deleted = !error && !RuntimeBinaryExists(runtime_directory);
        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_runtime_downloading_ = false;
            ai_runtime_progress_visible_ = false;
            ai_runtime_extracting_ = false;
            ai_runtime_download_url_.clear();
            ai_runtime_asset_name_.clear();
            ai_progress_ = 0.0f;
            ai_runtime_downloaded_bytes_ = 0;
            ai_runtime_total_bytes_ = 0;
            ai_status_ = deleted ? "Runtime deleted" : "Delete runtime failed";
        }
        RequestRedraw();
    });
}

void AssistantSettingsModalContent::CancelAiRuntimeDelete() {
    std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
    ai_runtime_delete_confirm_visible_ = false;
}

void AssistantSettingsModalContent::StartAiModelDownload() {
    using namespace assistant_modal_detail;

    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        if (ai_runtime_downloading_ || ai_model_downloading_ || ai_model_deleting_) {
            return;
        }
    }
    if (ai_model_thread_.joinable()) {
        ai_model_thread_.join();
    }

    Json model;
    if (!FindSelectedAiModel(selected_ai_model_, &model)) {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_status_ = "Model download failed";
        return;
    }

    const std::string url = JsonString(model, "model_url");
    if (url.empty()) {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_status_ = "Model URL is missing";
        return;
    }

    const std::string filename = JsonString(model, "filename");
    if (filename.empty()) {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_status_ = "Model filename is missing";
        return;
    }

    const std::filesystem::path final_path =
        UserDataDirectory() / "ai" / "models" / filename;
    const std::filesystem::path part_path =
        DownloadCacheDirectory() / (filename + ".part");

    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_model_downloading_ = true;
        ai_model_progress_visible_ = true;
        ai_progress_ = 0.0f;
        ai_model_downloaded_bytes_ = 0;
        ai_model_total_bytes_ = 0;
        ai_status_ = "Model downloading...";
        ai_model_delete_confirm_visible_ = false;
    }
    RequestRedraw();

    ai_model_thread_ = std::thread([this, url, final_path, part_path] {
        const bool ok = DownloadAiModelFile(
            url,
            final_path,
            part_path,
            ai_runtime_mutex_,
            ai_progress_,
            ai_model_downloaded_bytes_,
            ai_model_total_bytes_,
            request_redraw_);

        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_model_downloading_ = false;
            ai_model_progress_visible_ = false;
            ai_progress_ = 0.0f;
            ai_model_downloaded_bytes_ = 0;
            ai_model_total_bytes_ = 0;
            ai_refresh_after_model_download_ = ok;
            ai_status_ = ok ? "Model downloaded" : "Model download failed";
        }
        RequestRedraw();
    });
}

void AssistantSettingsModalContent::StartAiModelDelete() {
    using namespace assistant_modal_detail;

    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        if (ai_runtime_downloading_ || ai_model_downloading_ || ai_model_deleting_) {
            return;
        }
    }
    if (ai_model_thread_.joinable()) {
        ai_model_thread_.join();
    }

    Json model;
    if (!FindSelectedAiModel(selected_ai_model_, &model)) {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_model_delete_confirm_visible_ = false;
        ai_model_delete_pending_filename_.clear();
        ai_status_ = "No model selected";
        return;
    }

    const std::string filename = JsonString(model, "filename");
    if (filename.empty()) {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_model_delete_confirm_visible_ = false;
        ai_model_delete_pending_filename_.clear();
        ai_status_ = "Model filename is missing";
        return;
    }

    const std::filesystem::path model_path =
        UserDataDirectory() / "ai" / "models" / filename;
    std::error_code error;
    if (!std::filesystem::exists(model_path, error)) {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_model_delete_confirm_visible_ = false;
        ai_model_delete_pending_filename_.clear();
        ai_status_ = "Model not installed";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_model_delete_pending_filename_ = filename;
        ai_model_delete_confirm_visible_ = true;
        ai_model_progress_visible_ = false;
        ai_status_ = "Confirm model delete";
    }
    if (ai_model_confirm_delete_button_) {
        ai_model_confirm_delete_button_->TakeFocus();
    }
}

void AssistantSettingsModalContent::ConfirmAiModelDelete() {
    using namespace assistant_modal_detail;

    std::string filename;
    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        if (ai_runtime_downloading_ || ai_model_downloading_ || ai_model_deleting_) {
            return;
        }
        filename = ai_model_delete_pending_filename_;
    }
    if (ai_model_thread_.joinable()) {
        ai_model_thread_.join();
    }
    if (filename.empty()) {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_model_delete_confirm_visible_ = false;
        ai_status_ = "No model selected";
        return;
    }

    const std::filesystem::path model_path =
        UserDataDirectory() / "ai" / "models" / filename;
    const std::filesystem::path cache_directory = DownloadCacheDirectory();
    const std::filesystem::path part_path =
        cache_directory.empty() ? std::filesystem::path{} : cache_directory / (filename + ".part");
    std::error_code exists_error;
    if (!std::filesystem::exists(model_path, exists_error)) {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_model_delete_confirm_visible_ = false;
        ai_model_delete_pending_filename_.clear();
        ai_status_ = "Model not installed";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_model_deleting_ = true;
        ai_model_progress_visible_ = true;
        ai_model_delete_confirm_visible_ = false;
        ai_progress_ = 0.0f;
        ai_model_downloaded_bytes_ = 0;
        ai_model_total_bytes_ = 0;
        ai_status_ = "Model deleting...";
    }
    RequestRedraw();

    ai_model_thread_ = std::thread([this, model_path, part_path] {
        for (int step = 1; step <= 60; ++step) {
            {
                std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
                ai_progress_ = static_cast<float>(step) / 60.0f;
            }
            RequestRedraw();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::error_code remove_error;
        std::filesystem::remove(model_path, remove_error);
        std::error_code part_error;
        if (!part_path.empty()) {
            std::filesystem::remove(part_path, part_error);
        }

        std::error_code verify_error;
        const bool still_exists = std::filesystem::exists(model_path, verify_error);
        const bool deleted = !remove_error && !verify_error && !still_exists;
        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_model_deleting_ = false;
            ai_model_progress_visible_ = false;
            ai_model_delete_pending_filename_.clear();
            ai_progress_ = 0.0f;
            ai_model_downloaded_bytes_ = 0;
            ai_model_total_bytes_ = 0;
            ai_refresh_after_model_download_ = true;
            ai_status_ = deleted ? "Model deleted" : "Delete model failed";
        }
        RequestRedraw();
    });
}

void AssistantSettingsModalContent::CancelAiModelDelete() {
    std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
    ai_model_delete_confirm_visible_ = false;
    ai_model_delete_pending_filename_.clear();
}

AssistantSettingsModal::AssistantSettingsModal(
    const Theme* theme,
    std::function<void()> request_redraw)
    : theme_(theme) {
    content_ = std::make_shared<AssistantSettingsModalContent>(
        theme_,
        std::move(request_redraw));
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterText("Placeholder only. Escape closes.");
    modal_->SetFooterButtons({{"Close", [this] { Close(); }}});
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component AssistantSettingsModal::View() const {
    return modal_;
}

void AssistantSettingsModal::Open() {
    open_ = true;
    content_->SetTheme(theme_);
    modal_->SetTheme(theme_);
    content_->CloseTtsTestPopup();
    content_->GetMainComponent()->TakeFocus();
}

void AssistantSettingsModal::Close() {
    open_ = false;
}

bool AssistantSettingsModal::IsOpen() const {
    return open_;
}

bool AssistantSettingsModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}

} // namespace textlt
