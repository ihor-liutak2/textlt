#include "ai/ai_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <utility>

#include "json_utils.hpp"
#include "remote/remote_command_runner.hpp"
#include "remote/remote_http_client.hpp"

namespace textlt {
namespace {

std::filesystem::path AiUserDataDirectory() {
#ifdef _WIN32
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data && *local_app_data) {
        return std::filesystem::path(local_app_data) / "textlt";
    }
    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && *user_profile) {
        return std::filesystem::path(user_profile) / "AppData" / "Local" / "textlt";
    }
    return std::filesystem::path(".") / "textlt-data";
#else
    const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home && *xdg_data_home) {
        return std::filesystem::path(xdg_data_home) / "textlt";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home) / ".local" / "share" / "textlt";
    }
    return std::filesystem::path(".") / ".textlt";
#endif
}

std::string Trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string JoinUrl(const std::string& base, const std::string& path) {
    return AiBackend::NormalizeServerUrl(base) + path;
}

Json ParseJsonBody(const RemoteHttpResponse& response, std::string& error) {
    if (!response.ok) {
        error = response.error.empty()
            ? "AI server returned HTTP " + std::to_string(response.status_code) + "."
            : response.error;
        return Json();
    }
    Json root = Json::parse(response.body, nullptr, false);
    if (root.is_discarded()) {
        error = "AI server returned invalid JSON.";
        return Json();
    }
    return root;
}

std::filesystem::path FindLlamaCli() {
    const std::filesystem::path directory =
        AiUserDataDirectory() / "ai" / "runtimes" / "llama_cpp";
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        return {};
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error)) {
        if (error) {
            return {};
        }
        const std::string filename = entry.path().filename().string();
        if (entry.is_regular_file() && (filename == "llama-cli" || filename == "llama-cli.exe")) {
            return entry.path();
        }
    }
    return {};
}

bool IsSafeLocalModelFilename(const std::string& filename) {
    if (filename.empty()) {
        return false;
    }
    const std::filesystem::path path(filename);
    return !path.is_absolute() && path.parent_path().empty() &&
        path.filename() == path && filename != "." && filename != "..";
}

std::filesystem::path MakePromptFilePath() {
    const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::filesystem::temp_directory_path() /
        ("textlt-ai-prompt-" + std::to_string(ticks) + "-" +
         std::to_string(thread_id) + ".txt");
}

bool WritePromptFile(const std::filesystem::path& path, const std::string& prompt) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(prompt.data(), static_cast<std::streamsize>(prompt.size()));
    return static_cast<bool>(file);
}

std::string LocalPrompt(const AiPromptRequest& request) {
    return "System:\n" + BuildAiSystemPrompt(request) +
        "\n\nUser:\n" + BuildAiUserPrompt(request) +
        "\n\nAssistant:\n";
}

std::string CleanLocalOutput(std::string output, const std::string& prompt) {
    output = Trim(std::move(output));
    const std::size_t prompt_position = output.find(prompt);
    if (prompt_position != std::string::npos) {
        output.erase(prompt_position, prompt.size());
        output = Trim(std::move(output));
    }
    const std::string assistant_marker = "Assistant:";
    const std::size_t marker_position = output.rfind(assistant_marker);
    if (marker_position != std::string::npos) {
        output.erase(0, marker_position + assistant_marker.size());
        output = Trim(std::move(output));
    }
    return output;
}

bool IsCancelled(const std::atomic<bool>* cancel_requested) {
    return cancel_requested && cancel_requested->load();
}

void ConsumeCompleteLines(
    std::string& buffer,
    const std::string& chunk,
    const std::function<void(std::string)>& consume) {
    buffer += chunk;
    while (true) {
        const std::size_t newline = buffer.find('\n');
        if (newline == std::string::npos) {
            return;
        }
        std::string line = buffer.substr(0, newline);
        buffer.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        consume(std::move(line));
    }
}

std::string OllamaStreamContent(const std::string& line) {
    if (line.empty()) {
        return {};
    }
    const Json root = Json::parse(line, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return {};
    }
    const auto message = root.find("message");
    return message != root.end() && message->is_object()
        ? JsonString(*message, "content")
        : std::string{};
}

std::string OpenAiStreamContent(std::string line) {
    const std::string prefix = "data:";
    if (line.rfind(prefix, 0) != 0) {
        return {};
    }
    line = Trim(line.substr(prefix.size()));
    if (line.empty() || line == "[DONE]") {
        return {};
    }
    const Json root = Json::parse(line, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return {};
    }
    const auto choices = root.find("choices");
    if (choices == root.end() || !choices->is_array() || choices->empty() ||
        !(*choices)[0].is_object()) {
        return {};
    }
    const auto delta = (*choices)[0].find("delta");
    return delta != (*choices)[0].end() && delta->is_object()
        ? JsonString(*delta, "content")
        : std::string{};
}

int DefaultLocalThreadCount() {
    const unsigned int available = std::thread::hardware_concurrency();
    if (available <= 2) {
        return 1;
    }
    return static_cast<int>(std::min<unsigned int>(6, std::max<unsigned int>(2, available / 2)));
}

int DefaultMaxOutputTokens(const AiPromptRequest& request) {
    const std::size_t estimated_input_tokens = std::max<std::size_t>(32, request.text.size() / 3);
    const std::size_t estimated = request.action == AiActionType::Translate
        ? estimated_input_tokens * 2
        : estimated_input_tokens + estimated_input_tokens / 2;
    return static_cast<int>(std::clamp<std::size_t>(estimated, 128, 2048));
}

AiBackendResult ErrorResult(AiProvider provider, std::string error) {
    AiBackendResult result;
    result.provider = provider;
    result.error = std::move(error);
    return result;
}

AiBackendResult StoppedResult(AiProvider provider) {
    return ErrorResult(provider, "Operation stopped.");
}

} // namespace

AiBackend::AiBackend(AiBackendSettings settings)
    : settings_(std::move(settings)) {
    settings_.server_url = NormalizeServerUrl(settings_.server_url);
    if (settings_.timeout_seconds <= 0) {
        settings_.timeout_seconds = 120;
    }
}

AiProvider AiBackend::ProviderFromConfig(const std::string& value) {
    if (value == "ollama") {
        return AiProvider::Ollama;
    }
    if (value == "openai") {
        return AiProvider::OpenAiCompatible;
    }
    if (value == "llama_cpp") {
        return AiProvider::LocalLlamaCpp;
    }
    return AiProvider::Auto;
}

std::string AiBackend::ProviderToConfig(AiProvider provider) {
    switch (provider) {
        case AiProvider::Ollama:
            return "ollama";
        case AiProvider::OpenAiCompatible:
            return "openai";
        case AiProvider::LocalLlamaCpp:
            return "llama_cpp";
        case AiProvider::Auto:
        default:
            return "auto";
    }
}

std::string AiBackend::ProviderLabel(AiProvider provider) {
    switch (provider) {
        case AiProvider::Ollama:
            return "Ollama";
        case AiProvider::OpenAiCompatible:
            return "OpenAI-compatible";
        case AiProvider::LocalLlamaCpp:
            return "llama.cpp";
        case AiProvider::Auto:
        default:
            return "Auto";
    }
}

std::string AiBackend::NormalizeServerUrl(std::string url) {
    url = Trim(std::move(url));
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    if (url.size() >= 4 && url.compare(url.size() - 4, 4, "/api") == 0) {
        url.erase(url.size() - 4);
    } else if (url.size() >= 3 && url.compare(url.size() - 3, 3, "/v1") == 0) {
        url.erase(url.size() - 3);
    }
    return url;
}

bool AiBackend::IsSupportedServerUrl(const std::string& url) {
    const std::string normalized = NormalizeServerUrl(url);
    return normalized.rfind("http://", 0) == 0 || normalized.rfind("https://", 0) == 0;
}

std::string AiBackend::ModelIdFromKey(const std::string& key) {
    const std::size_t separator = key.find(':');
    return separator == std::string::npos ? key : key.substr(separator + 1);
}

AiConnectionResult AiBackend::TryOllama(
    const std::atomic<bool>* cancel_requested,
    RemoteCommandControl* command_control) const {
    AiConnectionResult result;
    result.provider = AiProvider::Ollama;
    result.provider_label = ProviderLabel(result.provider);
    if (settings_.server_url.empty()) {
        result.error = "AI server URL is empty.";
        return result;
    }
    if (!IsSupportedServerUrl(settings_.server_url)) {
        result.error = "AI server URL must start with http:// or https://.";
        return result;
    }

    RemoteHttpClient client;
    const RemoteHttpResponse response = client.RequestStreaming(
        "GET",
        JoinUrl(settings_.server_url, "/api/tags"),
        {"Accept: application/json"},
        {},
        std::min(settings_.timeout_seconds, 20),
        cancel_requested,
        {},
        command_control);
    if (IsCancelled(cancel_requested)) {
        result.error = "Operation stopped.";
        return result;
    }
    std::string error;
    const Json root = ParseJsonBody(response, error);
    if (!error.empty()) {
        result.error = std::move(error);
        return result;
    }

    const auto models = root.find("models");
    if (models == root.end() || !models->is_array()) {
        result.error = "Ollama response does not contain a models array.";
        return result;
    }
    for (const Json& item : *models) {
        if (!item.is_object()) {
            continue;
        }
        std::string id = JsonString(item, "name");
        if (id.empty()) {
            id = JsonString(item, "model");
        }
        if (id.empty()) {
            continue;
        }
        result.models.push_back({
            "ollama:" + id,
            id,
            id,
            "Ollama",
            {},
            AiModelSource::Server,
            true,
            false,
        });
    }
    result.success = true;
    return result;
}

AiConnectionResult AiBackend::TryOpenAiCompatible(
    const std::atomic<bool>* cancel_requested,
    RemoteCommandControl* command_control) const {
    AiConnectionResult result;
    result.provider = AiProvider::OpenAiCompatible;
    result.provider_label = ProviderLabel(result.provider);
    if (settings_.server_url.empty()) {
        result.error = "AI server URL is empty.";
        return result;
    }
    if (!IsSupportedServerUrl(settings_.server_url)) {
        result.error = "AI server URL must start with http:// or https://.";
        return result;
    }

    RemoteHttpClient client;
    const RemoteHttpResponse response = client.RequestStreaming(
        "GET",
        JoinUrl(settings_.server_url, "/v1/models"),
        {"Accept: application/json"},
        {},
        std::min(settings_.timeout_seconds, 20),
        cancel_requested,
        {},
        command_control);
    if (IsCancelled(cancel_requested)) {
        result.error = "Operation stopped.";
        return result;
    }
    std::string error;
    const Json root = ParseJsonBody(response, error);
    if (!error.empty()) {
        result.error = std::move(error);
        return result;
    }

    const auto data = root.find("data");
    if (data == root.end() || !data->is_array()) {
        result.error = "OpenAI-compatible response does not contain a data array.";
        return result;
    }
    for (const Json& item : *data) {
        if (!item.is_object()) {
            continue;
        }
        const std::string id = JsonString(item, "id");
        if (id.empty()) {
            continue;
        }
        result.models.push_back({
            "openai:" + id,
            id,
            id,
            "OpenAI-compatible",
            {},
            AiModelSource::Server,
            true,
            false,
        });
    }
    result.success = true;
    return result;
}

AiConnectionResult AiBackend::CheckConnectionAndListModels(
    const std::atomic<bool>* cancel_requested,
    RemoteCommandControl* command_control) const {
    if (settings_.provider == AiProvider::Ollama) {
        return TryOllama(cancel_requested, command_control);
    }
    if (settings_.provider == AiProvider::OpenAiCompatible) {
        return TryOpenAiCompatible(cancel_requested, command_control);
    }
    if (settings_.provider == AiProvider::LocalLlamaCpp) {
        AiConnectionResult result;
        result.provider = AiProvider::LocalLlamaCpp;
        result.provider_label = ProviderLabel(result.provider);
        result.success = !FindLlamaCli().empty();
        if (!result.success) {
            result.error = "llama.cpp runtime is not installed.";
        }
        return result;
    }

    AiConnectionResult ollama = TryOllama(cancel_requested, command_control);
    if (ollama.success || IsCancelled(cancel_requested)) {
        return ollama;
    }
    AiConnectionResult openai = TryOpenAiCompatible(cancel_requested, command_control);
    if (openai.success) {
        return openai;
    }
    openai.error = "Could not detect an Ollama or OpenAI-compatible server. Ollama: " +
        ollama.error + " OpenAI-compatible: " + openai.error;
    return openai;
}

AiBackendResult AiBackend::RunOllama(
    const AiPromptRequest& request,
    const std::string& model,
    const std::atomic<bool>* cancel_requested,
    ProgressCallback progress,
    RemoteCommandControl* command_control) const {
    Json body = {
        {"model", model},
        {"stream", true},
        {"messages", Json::array({
            Json{{"role", "system"}, {"content", BuildAiSystemPrompt(request)}},
            Json{{"role", "user"}, {"content", BuildAiUserPrompt(request)}},
        })},
        {"options", Json{
            {"temperature", 0.2},
            {"num_predict", settings_.max_output_tokens > 0
                ? settings_.max_output_tokens
                : DefaultMaxOutputTokens(request)},
        }},
    };

    std::string line_buffer;
    std::string generated;
    auto last_progress_at = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto consume_line = [&](std::string line) {
        const std::string content = OllamaStreamContent(line);
        if (content.empty()) {
            return;
        }
        generated += content;
        const auto now = std::chrono::steady_clock::now();
        if (progress && now - last_progress_at >= std::chrono::milliseconds(125)) {
            last_progress_at = now;
            progress(generated);
        }
    };

    RemoteHttpClient client;
    const RemoteHttpResponse response = client.RequestStreaming(
        "POST",
        JoinUrl(settings_.server_url, "/api/chat"),
        {"Accept: application/x-ndjson", "Content-Type: application/json"},
        body.dump(),
        settings_.timeout_seconds,
        cancel_requested,
        [&](const std::string& chunk) {
            ConsumeCompleteLines(line_buffer, chunk, consume_line);
        },
        command_control);
    if (!line_buffer.empty()) {
        consume_line(std::move(line_buffer));
    }
    if (IsCancelled(cancel_requested)) {
        return StoppedResult(AiProvider::Ollama);
    }
    if (!response.ok) {
        return ErrorResult(
            AiProvider::Ollama,
            response.error.empty() ? "Ollama request failed." : response.error);
    }
    if (generated.empty()) {
        std::string error;
        const Json root = ParseJsonBody(response, error);
        if (error.empty()) {
            const auto message = root.find("message");
            generated = message != root.end() && message->is_object()
                ? JsonString(*message, "content")
                : std::string{};
        }
    }
    generated = Trim(std::move(generated));
    if (generated.empty()) {
        return ErrorResult(AiProvider::Ollama, "Ollama returned an empty response.");
    }
    AiBackendResult result;
    result.success = true;
    result.provider = AiProvider::Ollama;
    result.text = std::move(generated);
    return result;
}

AiBackendResult AiBackend::RunOpenAiCompatible(
    const AiPromptRequest& request,
    const std::string& model,
    const std::atomic<bool>* cancel_requested,
    ProgressCallback progress,
    RemoteCommandControl* command_control) const {
    Json body = {
        {"model", model},
        {"temperature", 0.2},
        {"max_tokens", settings_.max_output_tokens > 0
            ? settings_.max_output_tokens
            : DefaultMaxOutputTokens(request)},
        {"stream", true},
        {"messages", Json::array({
            Json{{"role", "system"}, {"content", BuildAiSystemPrompt(request)}},
            Json{{"role", "user"}, {"content", BuildAiUserPrompt(request)}},
        })},
    };

    std::string line_buffer;
    std::string generated;
    auto last_progress_at = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto consume_line = [&](std::string line) {
        const std::string content = OpenAiStreamContent(std::move(line));
        if (content.empty()) {
            return;
        }
        generated += content;
        const auto now = std::chrono::steady_clock::now();
        if (progress && now - last_progress_at >= std::chrono::milliseconds(125)) {
            last_progress_at = now;
            progress(generated);
        }
    };

    RemoteHttpClient client;
    const RemoteHttpResponse response = client.RequestStreaming(
        "POST",
        JoinUrl(settings_.server_url, "/v1/chat/completions"),
        {"Accept: text/event-stream", "Content-Type: application/json"},
        body.dump(),
        settings_.timeout_seconds,
        cancel_requested,
        [&](const std::string& chunk) {
            ConsumeCompleteLines(line_buffer, chunk, consume_line);
        },
        command_control);
    if (!line_buffer.empty()) {
        consume_line(std::move(line_buffer));
    }
    if (IsCancelled(cancel_requested)) {
        return StoppedResult(AiProvider::OpenAiCompatible);
    }
    if (!response.ok) {
        return ErrorResult(
            AiProvider::OpenAiCompatible,
            response.error.empty()
                ? "OpenAI-compatible request failed."
                : response.error);
    }
    if (generated.empty()) {
        std::string error;
        const Json root = ParseJsonBody(response, error);
        if (error.empty()) {
            const auto choices = root.find("choices");
            if (choices != root.end() && choices->is_array() && !choices->empty() &&
                (*choices)[0].is_object()) {
                const auto message = (*choices)[0].find("message");
                generated = message != (*choices)[0].end() && message->is_object()
                    ? JsonString(*message, "content")
                    : std::string{};
            }
        }
    }
    generated = Trim(std::move(generated));
    if (generated.empty()) {
        return ErrorResult(
            AiProvider::OpenAiCompatible,
            "OpenAI-compatible server returned an empty response.");
    }
    AiBackendResult result;
    result.success = true;
    result.provider = AiProvider::OpenAiCompatible;
    result.text = std::move(generated);
    return result;
}

AiBackendResult AiBackend::RunLocalLlamaCpp(
    const AiPromptRequest& request,
    const std::string& filename,
    const std::atomic<bool>* cancel_requested,
    ProgressCallback progress,
    RemoteCommandControl* command_control) const {
    if (!IsSafeLocalModelFilename(filename)) {
        return ErrorResult(AiProvider::LocalLlamaCpp, "Selected local model filename is invalid.");
    }
    const std::filesystem::path runtime = FindLlamaCli();
    if (runtime.empty()) {
        return ErrorResult(AiProvider::LocalLlamaCpp, "llama.cpp runtime is not installed.");
    }
    const std::filesystem::path model_path = AiUserDataDirectory() / "ai" / "models" / filename;
    std::error_code error;
    if (!std::filesystem::exists(model_path, error)) {
        return ErrorResult(
            AiProvider::LocalLlamaCpp,
            "Selected local model is not downloaded: " + filename);
    }

    const std::string prompt = LocalPrompt(request);
    const std::filesystem::path prompt_path = MakePromptFilePath();
    if (!WritePromptFile(prompt_path, prompt)) {
        return ErrorResult(
            AiProvider::LocalLlamaCpp,
            "Could not create a temporary prompt file.");
    }
    const int max_output_tokens = settings_.max_output_tokens > 0
        ? settings_.max_output_tokens
        : DefaultMaxOutputTokens(request);
    const int local_threads = settings_.local_threads > 0
        ? settings_.local_threads
        : DefaultLocalThreadCount();

    std::string streamed_output;
    auto last_progress_at = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    RemoteCommandRunner runner;
    const RemoteCommandResult command = runner.RunStreaming({
        runtime.string(),
        "--model", model_path.string(),
        "--file", prompt_path.string(),
        "--n-predict", std::to_string(max_output_tokens),
        "--threads", std::to_string(local_threads),
        "--threads-batch", std::to_string(local_threads),
        "--prio", "-1",
        "--poll", "0",
        "--temp", "0.2",
        "--no-conversation",
        "--no-display-prompt",
        "--simple-io",
    }, {}, cancel_requested, [&](const std::string& chunk) {
        streamed_output += chunk;
        if (!progress) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - last_progress_at < std::chrono::milliseconds(125)) {
            return;
        }
        last_progress_at = now;
        const std::string cleaned = CleanLocalOutput(streamed_output, prompt);
        if (!cleaned.empty()) {
            progress(cleaned);
        }
    }, RemoteCommandOptions{settings_.timeout_seconds, 300, true}, command_control);
    std::filesystem::remove(prompt_path, error);
    if (command.timed_out) {
        return ErrorResult(AiProvider::LocalLlamaCpp, command.error);
    }
    if (command.cancelled || IsCancelled(cancel_requested)) {
        return StoppedResult(AiProvider::LocalLlamaCpp);
    }
    if (command.exit_code != 0) {
        return ErrorResult(
            AiProvider::LocalLlamaCpp,
            command.error.empty()
                ? "llama-cli exited with code " + std::to_string(command.exit_code) + "."
                : command.error);
    }
    const std::string output = CleanLocalOutput(command.output, prompt);
    if (output.empty()) {
        return ErrorResult(AiProvider::LocalLlamaCpp, "llama-cli returned an empty response.");
    }
    AiBackendResult result;
    result.success = true;
    result.provider = AiProvider::LocalLlamaCpp;
    result.text = output;
    return result;
}

AiBackendResult AiBackend::Run(
    const AiPromptRequest& request,
    const std::atomic<bool>* cancel_requested,
    ProgressCallback progress,
    RemoteCommandControl* command_control) const {
    if (IsCancelled(cancel_requested)) {
        return StoppedResult(settings_.provider);
    }
    if (request.text.empty()) {
        return ErrorResult(settings_.provider, "There is no text to process.");
    }
    if (settings_.selected_model_key.empty()) {
        return ErrorResult(settings_.provider, "No AI model is selected.");
    }

    const std::string model = ModelIdFromKey(settings_.selected_model_key);
    if (settings_.selected_model_key.rfind("local:", 0) == 0) {
        return RunLocalLlamaCpp(request, model, cancel_requested, progress, command_control);
    }
    if (settings_.selected_model_key.rfind("ollama:", 0) == 0) {
        return RunOllama(request, model, cancel_requested, progress, command_control);
    }
    if (settings_.selected_model_key.rfind("openai:", 0) == 0) {
        return RunOpenAiCompatible(request, model, cancel_requested, progress, command_control);
    }

    if (settings_.provider == AiProvider::LocalLlamaCpp) {
        return RunLocalLlamaCpp(request, model, cancel_requested, progress, command_control);
    }
    if (settings_.provider == AiProvider::Ollama) {
        return RunOllama(request, model, cancel_requested, progress, command_control);
    }
    if (settings_.provider == AiProvider::OpenAiCompatible) {
        return RunOpenAiCompatible(request, model, cancel_requested, progress, command_control);
    }

    const AiConnectionResult connection = CheckConnectionAndListModels(cancel_requested, command_control);
    if (!connection.success) {
        return ErrorResult(AiProvider::Auto, connection.error);
    }
    if (connection.provider == AiProvider::Ollama) {
        return RunOllama(request, model, cancel_requested, progress, command_control);
    }
    return RunOpenAiCompatible(request, model, cancel_requested, progress, command_control);
}

} // namespace textlt
