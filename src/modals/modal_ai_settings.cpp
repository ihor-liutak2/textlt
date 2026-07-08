#include "modals/modal_ai_settings.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <utility>

#include <archive.h>
#include <archive_entry.h>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

#include "assistant_download_progress.hpp"
#include "assistant_modals.hpp"
#include "curl_manager.hpp"
#include "ui_button.hpp"

namespace textlt {
namespace {

using assistant_modal_detail::DownloadCacheDirectory;
using assistant_modal_detail::DownloadRegistry;
using assistant_modal_detail::EnsureDirectory;
using assistant_modal_detail::JsonLabel;
using assistant_modal_detail::LoadUserRegistryJson;
using assistant_modal_detail::RegistryDownloadResult;
using assistant_modal_detail::RegistryFilename;
using assistant_modal_detail::RegistryKind;
using assistant_modal_detail::RegistryLoadResult;
using assistant_modal_detail::UserDataDirectory;

ftxui::Element StatusLine(const std::string& label,
                          const std::string& value,
                          const Theme& theme) {
    using namespace ftxui;
    return hbox({
        text(" " + label + ": ") | bold | color(theme.modal_accent),
        text(value) | color(theme.modal_text_color),
    });
}

ButtonSpec AiSettingsButtonSpec(std::string label) {
    ButtonSpec spec = ButtonSpecFromLabel(std::move(label));
    spec.variant = ButtonVariant::AccentEdges;
    return spec;
}

ftxui::Component MakeAiSettingsButton(
    const Theme** theme,
    std::string label,
    std::function<void()> on_click) {
    ButtonSpec spec = AiSettingsButtonSpec(std::move(label));
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(on_click);
    option.transform = [theme, spec = std::move(spec)](const ftxui::EntryState& state) {
        const Theme& resolved = (theme && *theme) ? **theme : FallbackTheme();
        return RenderModalFlatButton(resolved, spec, state.focused || state.active);
    };
    return ftxui::Button(option);
}

std::filesystem::path AiRuntimeDirectory() {
    return UserDataDirectory() / "ai" / "runtimes" / "llama_cpp";
}

std::string AiRuntimeDisplayPath() {
#ifdef _WIN32
    return "%LOCALAPPDATA%\\textlt\\ai\\runtimes\\llama_cpp\\llama-cli.exe";
#else
    return "~/.local/share/textlt/ai/runtimes/llama_cpp/llama-cli";
#endif
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

bool AiRuntimeInstalled() {
    return RuntimeBinaryExists(AiRuntimeDirectory());
}

bool AiModelInstalled(const Json& model) {
    const std::string filename = JsonString(model, "filename");
    if (filename.empty()) {
        return false;
    }

    std::error_code error;
    return std::filesystem::exists(
        UserDataDirectory() / "ai" / "models" / filename,
        error);
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

std::string SelectedAiModelDescription(int selected_model) {
    Json model;
    if (!FindSelectedAiModel(selected_model, &model)) {
        return {};
    }
    return JsonString(model, "description");
}

bool DownloadRuntimeArchive(const std::string& url,
                            const std::filesystem::path& final_path,
                            std::mutex& state_mutex) {
    EnsureDirectory(final_path.parent_path());
    const std::filesystem::path part_path = final_path.string() + ".part";
    std::error_code error;
    std::filesystem::remove(part_path, error);

    const bool download_ok = CurlManager::DownloadToFile(
        url,
        part_path,
        [&state_mutex](unsigned long long, unsigned long long) {
            std::lock_guard<std::mutex> lock(state_mutex);
            return true;
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
                         std::mutex& state_mutex) {
    EnsureDirectory(final_path.parent_path());
    EnsureDirectory(part_path.parent_path());
    std::error_code error;
    std::filesystem::remove(part_path, error);

    const bool download_ok = CurlManager::DownloadToFile(
        url,
        part_path,
        [&state_mutex](unsigned long long, unsigned long long) {
            std::lock_guard<std::mutex> lock(state_mutex);
            return true;
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

std::vector<std::string> WrapText(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    std::string current;
    size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() && text[position] == ' ') {
            ++position;
        }
        const size_t start = position;
        while (position < text.size() && text[position] != ' ') {
            ++position;
        }
        std::string word = text.substr(start, position - start);
        if (word.empty()) {
            continue;
        }
        if (current.empty()) {
            current = std::move(word);
        } else if (current.size() + 1 + word.size() <= width) {
            current += " " + word;
        } else {
            lines.push_back(current);
            current = std::move(word);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

std::string TrimFooterText(const std::string& text) {
    constexpr size_t kMaxFooterTextLength = 60;
    if (text.size() <= kMaxFooterTextLength) {
        return text;
    }
    return text.substr(0, kMaxFooterTextLength);
}

} // namespace

AiSettingsModalContent::AiSettingsModalContent(
    const Theme* theme,
    std::function<void()> request_redraw)
    : theme_(theme),
      request_redraw_(std::move(request_redraw)) {
    auto make_button = [this](std::string label, std::function<void()> on_click) {
        return MakeAiSettingsButton(&theme_, std::move(label), std::move(on_click));
    };

    ftxui::MenuOption checkbox_option = ftxui::MenuOption::Vertical();
    checkbox_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(std::string(state.state ? "[x] " : "[ ] ") + state.label);
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return row | ftxui::color(theme.modal_text_color);
    };
    ai_model_menu_ = ftxui::Menu(&ai_model_labels_, &selected_ai_model_, checkbox_option);

    fetch_ai_button_ = make_button("Fetch registry", [this] { FetchAiRegistry(); });
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

    container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            ai_runtime_download_button_,
            ai_runtime_delete_button_,
            fetch_ai_button_,
            ai_model_download_button_,
            ai_delete_model_button_,
        }),
        ftxui::Container::Horizontal({
            ai_runtime_confirm_delete_button_,
            ai_runtime_cancel_delete_button_,
            ai_model_confirm_delete_button_,
            ai_model_cancel_delete_button_,
        }),
        ai_model_menu_,
    });

    LoadAiRegistry();
}

AiSettingsModalContent::~AiSettingsModalContent() {
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

ftxui::Element AiSettingsModalContent::Render() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return RenderAiSettings(theme) |
        ftxui::bgcolor(theme.modal_input_bg) |
        ftxui::color(theme.modal_input_fg);
}

ftxui::Element AiSettingsModalContent::RenderTitle() {
    return ftxui::text(GetTitle());
}

std::string AiSettingsModalContent::GetFooterText() const {
    std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
    return TrimFooterText(ai_status_);
}

void AiSettingsModalContent::LoadAiRegistry() {
    Json root;
    const RegistryLoadResult load_result = LoadUserRegistryJson(RegistryKind::Ai, &root);
    ai_model_labels_.clear();
    if (load_result == RegistryLoadResult::Loaded) {
        const auto models = root.find("models");
        if (models != root.end() && models->is_array()) {
            for (const Json& model : *models) {
                if (!model.is_object()) {
                    continue;
                }
                std::string label = JsonLabel(model, "title", "id");
                const std::string purpose = JsonString(model, "purpose");
                const std::string backend = JsonString(model, "backend");
                label += " | " + (purpose.empty() ? "unknown" : purpose);
                label += " | " + (backend.empty() ? "unknown" : backend);
                label += AiModelInstalled(model) ? " | installed" : " | not installed";
                ai_model_labels_.push_back(label);
            }
        }
    }
    if (ai_model_labels_.empty()) {
        ai_model_labels_.push_back("No models");
        if (load_result == RegistryLoadResult::Missing) {
            ai_status_ = "Registry not loaded";
        } else if (load_result == RegistryLoadResult::ParseFailed) {
            ai_status_ = "Failed to parse registry";
        } else {
            ai_status_ = "Registry loaded, no items found";
        }
    } else if (ai_status_.find("TODO:") != 0) {
        ai_status_ = "Registry loaded";
    }
    selected_ai_model_ = 0;
}

void AiSettingsModalContent::FetchAiRegistry() {
    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        if (ai_runtime_downloading_ || ai_model_downloading_ || ai_model_deleting_) {
            return;
        }
        ai_status_ = "Fetching AI registry...";
        ai_progress_ = 0.0f;
    }
    if (fetch_thread_.joinable()) {
        fetch_thread_.join();
    }
    RequestRedraw();

    fetch_thread_ = std::thread([this] {
        const RegistryDownloadResult download_result =
            DownloadRegistry(CurlManager::kAiRegistryUrl, RegistryFilename(RegistryKind::Ai));
        LoadAiRegistry();

        Json ai_root;
        const RegistryLoadResult load_result =
            LoadUserRegistryJson(RegistryKind::Ai, &ai_root);

        auto registry_status = [](RegistryDownloadResult result, RegistryLoadResult load) {
            if (result == RegistryDownloadResult::Saved && load == RegistryLoadResult::Loaded) {
                return std::string("AI registry loaded");
            }
            if (result == RegistryDownloadResult::Empty) {
                return std::string("AI registry file is empty");
            }
            if (result == RegistryDownloadResult::InvalidJson) {
                return std::string("Downloaded AI registry is invalid JSON");
            }
            if (load == RegistryLoadResult::Missing) {
                return std::string("AI registry not loaded");
            }
            if (load == RegistryLoadResult::ParseFailed) {
                return std::string("AI registry parse failed");
            }
            return std::string("AI registry download failed");
        };

        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_status_ = registry_status(download_result, load_result);
        }
        RequestRedraw();
    });
}

bool AiSettingsModalContent::ResolveAiRuntimeDownload() {
    EnsureDirectory(UserDataDirectory() / "ai" / "runtimes");
    EnsureDirectory(DownloadCacheDirectory());

    ai_runtime_download_url_.clear();
    ai_runtime_asset_name_.clear();

    const std::string url = AiRuntimeDownloadUrl();
    if (url.empty()) {
        ai_status_ = "Runtime URL not found";
        return false;
    }
    ai_runtime_download_url_ = url;
    ai_runtime_asset_name_ = std::filesystem::path(url).filename().string();
    if (ai_runtime_asset_name_.empty()) {
        ai_runtime_asset_name_ = "llama_cpp_runtime.tar.gz";
    }
    return true;
}

void AiSettingsModalContent::StartAiRuntimeDownload() {
    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        if (ai_runtime_downloading_ || ai_model_downloading_ || ai_model_deleting_) {
            return;
        }
        if (ai_runtime_download_url_.empty() && !ResolveAiRuntimeDownload()) {
            return;
        }
    }
    if (ai_runtime_thread_.joinable()) {
        ai_runtime_thread_.join();
    }

    const std::string url = ai_runtime_download_url_;
    const std::string asset_name = ai_runtime_asset_name_.empty()
        ? std::string("llama_cpp_runtime.tar.gz")
        : ai_runtime_asset_name_;
    const std::filesystem::path archive_path = DownloadCacheDirectory() / asset_name;
    const std::filesystem::path runtime_directory = AiRuntimeDirectory();

    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_runtime_downloading_ = true;
        ai_runtime_progress_visible_ = true;
        ai_runtime_extracting_ = false;
        ai_runtime_delete_confirm_visible_ = false;
        ai_progress_ = 0.0f;
        ai_status_ = "Runtime downloading...";
    }
    RequestRedraw();

    ai_runtime_thread_ = std::thread([this, url, archive_path, runtime_directory] {
        std::atomic_bool progress_running{true};
        std::thread progress_thread = assistant_modal_detail::StartAssistantDownloadProgress(
            progress_running,
            [this](float progress) { ai_progress_.store(progress); },
            request_redraw_);

        const bool download_ok = DownloadRuntimeArchive(url, archive_path, ai_runtime_mutex_);
        progress_running = false;
        if (progress_thread.joinable()) {
            progress_thread.join();
        }
        if (!download_ok) {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_runtime_downloading_ = false;
            ai_runtime_progress_visible_ = false;
            ai_progress_ = 0.0f;
            ai_status_ = "Runtime download failed";
            RequestRedraw();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_runtime_extracting_ = true;
            ai_progress_ = 0.0f;
            ai_status_ = "Runtime extracting...";
        }
        RequestRedraw();
        for (int step = 1; step <= 50; ++step) {
            {
                std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
                ai_progress_ = static_cast<float>(step) / 50.0f;
            }
            RequestRedraw();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::error_code remove_error;
        std::filesystem::remove_all(runtime_directory, remove_error);
        const bool extract_ok = ExtractRuntimeArchive(archive_path, runtime_directory);
        std::error_code archive_error;
        std::filesystem::remove(archive_path, archive_error);
        const bool binary_ok = extract_ok && RuntimeBinaryExists(runtime_directory);
        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_runtime_downloading_ = false;
            ai_runtime_progress_visible_ = false;
            ai_runtime_extracting_ = false;
            ai_progress_ = 0.0f;
            ai_status_ = binary_ok ? "Runtime installed" : "Runtime install failed";
        }
        RequestRedraw();
    });
}

void AiSettingsModalContent::StartAiRuntimeDelete() {
    const std::filesystem::path runtime_directory = AiRuntimeDirectory();
    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        if (ai_runtime_downloading_ || ai_model_downloading_ || ai_model_deleting_) {
            return;
        }
        if (!RuntimeBinaryExists(runtime_directory)) {
            ai_runtime_delete_confirm_visible_ = false;
            ai_status_ = "Runtime not installed";
            return;
        }
        ai_runtime_delete_confirm_visible_ = true;
        ai_runtime_progress_visible_ = false;
        ai_status_ = "Confirm runtime delete";
    }
    if (ai_runtime_confirm_delete_button_) {
        ai_runtime_confirm_delete_button_->TakeFocus();
    }
}

void AiSettingsModalContent::ConfirmAiRuntimeDelete() {
    const std::filesystem::path runtime_directory = AiRuntimeDirectory();
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
            ai_status_ = deleted ? "Runtime deleted" : "Delete runtime failed";
        }
        RequestRedraw();
    });
}

void AiSettingsModalContent::CancelAiRuntimeDelete() {
    std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
    ai_runtime_delete_confirm_visible_ = false;
}

void AiSettingsModalContent::StartAiModelDownload() {
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

    const std::filesystem::path final_path = UserDataDirectory() / "ai" / "models" / filename;
    const std::filesystem::path part_path = DownloadCacheDirectory() / (filename + ".part");

    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        ai_model_downloading_ = true;
        ai_model_progress_visible_ = true;
        ai_progress_ = 0.0f;
        ai_status_ = "Model downloading...";
        ai_model_delete_confirm_visible_ = false;
    }
    RequestRedraw();

    ai_model_thread_ = std::thread([this, url, final_path, part_path] {
        std::atomic_bool progress_running{true};
        std::thread progress_thread = assistant_modal_detail::StartAssistantDownloadProgress(
            progress_running,
            [this](float progress) { ai_progress_.store(progress); },
            request_redraw_);

        const bool ok = DownloadAiModelFile(url, final_path, part_path, ai_runtime_mutex_);

        progress_running = false;
        if (progress_thread.joinable()) {
            progress_thread.join();
        }

        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_model_downloading_ = false;
            ai_model_progress_visible_ = false;
            ai_progress_ = 0.0f;
            ai_refresh_after_model_download_ = ok;
            ai_status_ = ok ? "Model downloaded" : "Model download failed";
        }
        RequestRedraw();
    });
}

void AiSettingsModalContent::StartAiModelDelete() {
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

    const std::filesystem::path model_path = UserDataDirectory() / "ai" / "models" / filename;
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

void AiSettingsModalContent::ConfirmAiModelDelete() {
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

    const std::filesystem::path model_path = UserDataDirectory() / "ai" / "models" / filename;
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
            ai_refresh_after_model_download_ = true;
            ai_status_ = deleted ? "Model deleted" : "Delete model failed";
        }
        RequestRedraw();
    });
}

void AiSettingsModalContent::CancelAiModelDelete() {
    std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
    ai_model_delete_confirm_visible_ = false;
    ai_model_delete_pending_filename_.clear();
}

void AiSettingsModalContent::RequestRedraw() const {
    if (request_redraw_) {
        request_redraw_();
    }
}

ftxui::Element AiSettingsModalContent::RenderAiSettings(const Theme& theme) {
    using namespace ftxui;
    bool refresh_models = false;
    bool show_runtime_delete_confirmation = false;
    bool show_runtime_progress = false;
    bool show_extraction_progress = false;
    bool show_model_progress = false;
    bool show_model_delete_confirmation = false;
    bool show_model_delete_progress = false;
    {
        std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
        refresh_models = ai_refresh_after_model_download_;
        ai_refresh_after_model_download_ = false;
        show_runtime_delete_confirmation = ai_runtime_delete_confirm_visible_;
        show_runtime_progress = ai_runtime_progress_visible_;
        show_extraction_progress = ai_runtime_extracting_;
        show_model_progress = ai_model_progress_visible_;
        show_model_delete_confirmation = ai_model_delete_confirm_visible_;
        show_model_delete_progress = ai_model_deleting_;
    }
    if (refresh_models) {
        std::string status;
        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            status = ai_status_;
        }
        LoadAiRegistry();
        {
            std::lock_guard<std::mutex> lock(ai_runtime_mutex_);
            ai_status_ = status;
        }
    }
    const float ai_progress = std::max(0.0f, std::min(1.0f, ai_progress_.load()));

    Elements rows;
    rows.push_back(text(" Runtime") | bold | color(theme.modal_text_color));
    rows.push_back(StatusLine(
        "llama.cpp runtime",
        AiRuntimeInstalled() ? "installed" : "not installed",
        theme));
    rows.push_back(StatusLine("runtime path", AiRuntimeDisplayPath(), theme));
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(hbox({
        ai_runtime_download_button_->Render(),
        text(" "),
        ai_runtime_delete_button_->Render(),
    }));
    if (show_runtime_delete_confirmation) {
        rows.push_back(text(" Delete llama.cpp runtime?") |
                       bold |
                       color(theme.modal_text_color));
        rows.push_back(hbox({
            ai_runtime_confirm_delete_button_->Render(),
            text(" "),
            ai_runtime_cancel_delete_button_->Render(),
        }));
    }
    if (show_runtime_progress) {
        const int percent = static_cast<int>(ai_progress * 100.0f + 0.5f);
        rows.push_back(hbox({
            filler(),
            text(std::to_string(percent) + "% ") | color(theme.modal_text_color),
        }));
        rows.push_back(gauge(ai_progress) | borderStyled(LIGHT, theme.modal_border));
        if (show_extraction_progress) {
            rows.push_back(text(" Extracting runtime files...") | dim | color(theme.modal_text_color));
        }
    }
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(text(" Models") | bold | color(theme.modal_text_color));
    rows.push_back(hbox({
        fetch_ai_button_->Render(),
        text(" "),
        ai_model_download_button_->Render(),
        text(" "),
        ai_delete_model_button_->Render(),
    }));
    if (show_model_delete_confirmation) {
        rows.push_back(text(" Delete selected model?") |
                       bold |
                       color(theme.modal_text_color));
        rows.push_back(hbox({
            ai_model_confirm_delete_button_->Render(),
            text(" "),
            ai_model_cancel_delete_button_->Render(),
        }));
    }
    if (show_model_progress) {
        const int percent = static_cast<int>(ai_progress * 100.0f + 0.5f);
        rows.push_back(hbox({
            filler(),
            text(std::to_string(percent) + "% ") | color(theme.modal_text_color),
        }));
        rows.push_back(gauge(ai_progress) | borderStyled(LIGHT, theme.modal_border));
        if (show_model_delete_progress) {
            rows.push_back(text(" Deleting selected model...") | dim | color(theme.modal_text_color));
        }
    }
    rows.push_back(ai_model_menu_->Render() | borderStyled(LIGHT, theme.modal_border));
    const std::string description = SelectedAiModelDescription(selected_ai_model_);
    if (!description.empty()) {
        for (const std::string& line : WrapText(description, 76)) {
            rows.push_back(text(" " + line) |
                           color(theme.modal_text_color) |
                           dim);
        }
    }
    return vbox(std::move(rows)) | borderStyled(LIGHT, theme.modal_border);
}

AiSettingsModal::AiSettingsModal(
    const Theme* theme,
    std::function<void()> request_redraw)
    : theme_(theme) {
    content_ = std::make_shared<AiSettingsModalContent>(
        theme_,
        std::move(request_redraw));
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
