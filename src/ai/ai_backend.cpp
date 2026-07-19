#include "ai/ai_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <utility>

#include "ai/local_llama_server.hpp"
#include "json_utils.hpp"
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

bool IsSafeLocalModelFilename(const std::string& filename) {
    if (filename.empty()) {
        return false;
    }
    const std::filesystem::path path(filename);
    return !path.is_absolute() && path.parent_path().empty() &&
        path.filename() == path && filename != "." && filename != "..";
}

bool EndsWithAsciiInsensitive(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    const size_t offset = value.size() - suffix.size();
    for (size_t index = 0; index < suffix.size(); ++index) {
        const unsigned char left = static_cast<unsigned char>(value[offset + index]);
        const unsigned char right = static_cast<unsigned char>(suffix[index]);
        if (std::tolower(left) != std::tolower(right)) {
            return false;
        }
    }
    return true;
}

std::string NormalizeGeneratedTextImpl(std::string output) {
    output = Trim(std::move(output));
    static const std::vector<std::string> trailing_markers = {
        "[end of text]",
        "<|end_of_text|>",
        "<|eot_id|>",
        "<end_of_turn>",
        "</s>",
    };

    bool removed = true;
    while (removed && !output.empty()) {
        removed = false;
        for (const std::string& marker : trailing_markers) {
            if (!EndsWithAsciiInsensitive(output, marker)) {
                continue;
            }
            output.erase(output.size() - marker.size());
            output = Trim(std::move(output));
            removed = true;
            break;
        }
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

double JsonNumber(const Json& object, const char* key);
int JsonInteger(const Json& object, const char* key);

struct OllamaStreamEvent {
    std::string content;
    std::string done_reason;
    bool done = false;
    int prompt_tokens = 0;
    int generated_tokens = 0;
    double prompt_ms = 0.0;
    double generation_ms = 0.0;
    double tokens_per_second = 0.0;
};

OllamaStreamEvent ParseOllamaStreamEvent(const std::string& line) {
    OllamaStreamEvent event;
    if (line.empty()) {
        return event;
    }
    const Json root = Json::parse(line, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return event;
    }
    const auto message = root.find("message");
    if (message != root.end() && message->is_object()) {
        event.content = JsonString(*message, "content");
    }
    event.done = root.value("done", false);
    event.done_reason = JsonString(root, "done_reason");
    event.prompt_tokens = JsonInteger(root, "prompt_eval_count");
    event.generated_tokens = JsonInteger(root, "eval_count");
    const double prompt_ns = JsonNumber(root, "prompt_eval_duration");
    const double generation_ns = JsonNumber(root, "eval_duration");
    event.prompt_ms = prompt_ns > 0.0 ? prompt_ns / 1000000.0 : 0.0;
    event.generation_ms = generation_ns > 0.0 ? generation_ns / 1000000.0 : 0.0;
    if (event.generated_tokens > 0 && generation_ns > 0.0) {
        event.tokens_per_second =
            static_cast<double>(event.generated_tokens) * 1000000000.0 / generation_ns;
    }
    return event;
}

struct OpenAiStreamEvent {
    std::string content;
    std::string finish_reason;
    std::string stop_type;
    int prompt_tokens = 0;
    int generated_tokens = 0;
    double prompt_ms = 0.0;
    double generation_ms = 0.0;
    double tokens_per_second = 0.0;
};

double JsonNumber(const Json& object, const char* key) {
    const auto value = object.find(key);
    return value != object.end() && value->is_number()
        ? value->get<double>()
        : 0.0;
}

int JsonInteger(const Json& object, const char* key) {
    const auto value = object.find(key);
    return value != object.end() && value->is_number_integer()
        ? value->get<int>()
        : 0;
}

OpenAiStreamEvent ParseOpenAiStreamEvent(std::string line) {
    OpenAiStreamEvent event;
    const std::string prefix = "data:";
    if (line.rfind(prefix, 0) != 0) {
        return event;
    }
    line = Trim(line.substr(prefix.size()));
    if (line.empty() || line == "[DONE]") {
        return event;
    }
    const Json root = Json::parse(line, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return event;
    }
    const auto choices = root.find("choices");
    if (choices != root.end() && choices->is_array() && !choices->empty() &&
        (*choices)[0].is_object()) {
        const Json& choice = (*choices)[0];
        const auto delta = choice.find("delta");
        if (delta != choice.end() && delta->is_object()) {
            event.content = JsonString(*delta, "content");
        }
        event.finish_reason = JsonString(choice, "finish_reason");
        event.stop_type = JsonString(choice, "stop_type");
    }
    if (event.stop_type.empty()) {
        event.stop_type = JsonString(root, "stop_type");
    }
    const auto usage = root.find("usage");
    if (usage != root.end() && usage->is_object()) {
        event.prompt_tokens = JsonInteger(*usage, "prompt_tokens");
        event.generated_tokens = JsonInteger(*usage, "completion_tokens");
    }
    const auto timings = root.find("timings");
    if (timings != root.end() && timings->is_object()) {
        event.prompt_tokens = std::max(event.prompt_tokens, JsonInteger(*timings, "prompt_n"));
        event.generated_tokens = std::max(event.generated_tokens, JsonInteger(*timings, "predicted_n"));
        event.prompt_ms = JsonNumber(*timings, "prompt_ms");
        event.generation_ms = JsonNumber(*timings, "predicted_ms");
        event.tokens_per_second = JsonNumber(*timings, "predicted_per_second");
    }
    return event;
}

AiFinishReason FinishReasonFromOpenAi(
    const std::string& value,
    const std::string& stop_type) {
    if (stop_type == "word") {
        return AiFinishReason::StopMarker;
    }
    if (stop_type == "limit" || value == "length") {
        return AiFinishReason::TokenLimit;
    }
    if (stop_type == "eos" || value == "stop") {
        return AiFinishReason::Eos;
    }
    return value.empty() ? AiFinishReason::Unknown : AiFinishReason::Eos;
}

int DefaultLocalThreadCount() {
    const unsigned int available = std::thread::hardware_concurrency();
    if (available <= 2) {
        return 1;
    }
    return static_cast<int>(std::min<unsigned int>(6, std::max<unsigned int>(2, available / 2)));
}

int CountUtf8Codepoints(const std::string& text) {
    int count = 0;
    for (unsigned char ch : text) {
        if ((ch & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

int DefaultMaxOutputTokens(const AiPromptRequest& request) {
    const int codepoints = std::max(1, CountUtf8Codepoints(request.text));
    const int estimated_input_tokens = std::max(8, codepoints / 2 + 8);
    const int estimated = request.action == AiActionType::Translate
        ? estimated_input_tokens * 2 + 24
        : estimated_input_tokens + estimated_input_tokens / 2 + 24;
    return std::clamp(estimated, 64, 2048);
}

AiBackendResult ErrorResult(AiProvider provider, std::string error) {
    AiBackendResult result;
    result.provider = provider;
    result.error = std::move(error);
    result.finish_reason = AiFinishReason::Error;
    return result;
}

AiBackendResult StoppedResult(AiProvider provider) {
    AiBackendResult result = ErrorResult(provider, "Operation stopped.");
    result.finish_reason = AiFinishReason::Cancelled;
    return result;
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

std::string AiBackend::NormalizeGeneratedText(std::string text) {
    return NormalizeGeneratedTextImpl(std::move(text));
}

int AiBackend::RecommendedMaxOutputTokens(const AiPromptRequest& request) {
    return DefaultMaxOutputTokens(request);
}

std::string AiBackend::FinishReasonLabel(AiFinishReason reason) {
    switch (reason) {
        case AiFinishReason::Eos: return "end of sequence";
        case AiFinishReason::StopMarker: return "stop marker";
        case AiFinishReason::TokenLimit: return "token limit";
        case AiFinishReason::Timeout: return "timeout";
        case AiFinishReason::Cancelled: return "cancelled";
        case AiFinishReason::Error: return "error";
        case AiFinishReason::Unknown:
        default: return "completed";
    }
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
            false,
            0,
            {},
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
            false,
            0,
            {},
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
        result.success = LocalLlamaServerManager::Instance().RuntimeAvailable();
        if (!result.success) {
            result.error = "llama-server is not installed. Download a current llama.cpp runtime.";
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

AiConnectionResult AiBackend::CheckSelectedModelReady(
    const std::atomic<bool>* cancel_requested,
    RemoteCommandControl* command_control) const {
    AiConnectionResult result;
    if (settings_.selected_model_key.empty()) {
        result.provider = settings_.provider;
        result.provider_label = ProviderLabel(result.provider);
        result.error = "No AI model is selected.";
        return result;
    }
    if (IsCancelled(cancel_requested)) {
        result.provider = settings_.provider;
        result.provider_label = ProviderLabel(result.provider);
        result.error = "Operation stopped.";
        return result;
    }

    const std::string& selected_key = settings_.selected_model_key;
    const std::string model_id = ModelIdFromKey(selected_key);
    if (selected_key.rfind("local:", 0) == 0 ||
        (selected_key.find(':') == std::string::npos &&
         settings_.provider == AiProvider::LocalLlamaCpp)) {
        result.provider = AiProvider::LocalLlamaCpp;
        result.provider_label = ProviderLabel(result.provider);
        if (!LocalLlamaServerManager::Instance().RuntimeAvailable()) {
            result.error = "llama-server is not installed. Download a current llama.cpp runtime.";
            return result;
        }
        if (!IsSafeLocalModelFilename(model_id)) {
            result.error = "Selected local model filename is invalid.";
            return result;
        }
        std::error_code error;
        const std::filesystem::path model_path =
            AiUserDataDirectory() / "ai" / "models" / model_id;
        if (!std::filesystem::is_regular_file(model_path, error)) {
            result.error = "Selected local model is not downloaded: " + model_id;
            return result;
        }
        LocalLlamaServerManager& manager = LocalLlamaServerManager::Instance();
        std::string hardware_error;
        if (!manager.IsReadyFor(model_id) &&
            !manager.CheckModelHardware(
                model_id, hardware_error, cancel_requested, command_control)) {
            result.error = hardware_error;
            return result;
        }
        result.success = true;
        result.models = {{
            "local:" + model_id,
            model_id,
            model_id,
            "llama.cpp",
            model_id,
            AiModelSource::ManagedLocal,
            true,
            true,
            false,
            0,
            {},
        }};
        return result;
    }

    if (selected_key.rfind("ollama:", 0) == 0) {
        result = TryOllama(cancel_requested, command_control);
    } else if (selected_key.rfind("openai:", 0) == 0) {
        result = TryOpenAiCompatible(cancel_requested, command_control);
    } else {
        result = CheckConnectionAndListModels(cancel_requested, command_control);
    }
    if (!result.success || IsCancelled(cancel_requested)) {
        return result;
    }

    const auto selected = std::find_if(
        result.models.begin(), result.models.end(), [&](const AiModelInfo& model) {
            return model.key == selected_key || model.id == model_id;
        });
    if (selected == result.models.end()) {
        result.success = false;
        result.error = "The selected model is not available from the configured AI backend.";
    }
    return result;
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
            {"stop", Json::array({
                "[end of text]", "<|end_of_text|>", "<|eot_id|>",
                "<end_of_turn>", "</s>"})},
        }},
    };

    const auto request_started = std::chrono::steady_clock::now();
    bool first_token_seen = false;
    std::string line_buffer;
    std::string generated;
    std::string done_reason;
    AiBackendResult result;
    result.provider = AiProvider::Ollama;
    auto last_progress_at = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto consume_line = [&](std::string line) {
        const OllamaStreamEvent event = ParseOllamaStreamEvent(line);
        result.prompt_tokens = std::max(result.prompt_tokens, event.prompt_tokens);
        result.generated_tokens = std::max(result.generated_tokens, event.generated_tokens);
        result.prompt_ms = std::max(result.prompt_ms, event.prompt_ms);
        result.generation_ms = std::max(result.generation_ms, event.generation_ms);
        result.tokens_per_second = std::max(result.tokens_per_second, event.tokens_per_second);
        if (event.done && !event.done_reason.empty()) {
            done_reason = event.done_reason;
        }
        if (event.content.empty()) {
            return;
        }
        if (!first_token_seen) {
            first_token_seen = true;
            result.time_to_first_token_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - request_started).count();
        }
        generated += event.content;
        const auto now = std::chrono::steady_clock::now();
        if (progress && now - last_progress_at >= std::chrono::milliseconds(125)) {
            last_progress_at = now;
            progress(NormalizeGeneratedTextImpl(generated));
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
    if (IsCancelled(cancel_requested) ||
        (command_control && command_control->StopRequested())) {
        return StoppedResult(AiProvider::Ollama);
    }
    if (!response.ok) {
        result.error = response.error.empty() ? "Ollama request failed." : response.error;
        result.finish_reason = result.error.find("timed out") != std::string::npos
            ? AiFinishReason::Timeout
            : AiFinishReason::Error;
        return result;
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
    generated = NormalizeGeneratedTextImpl(std::move(generated));
    if (generated.empty()) {
        return ErrorResult(AiProvider::Ollama, "Ollama returned an empty response.");
    }
    result.success = true;
    result.text = std::move(generated);
    result.finish_reason = done_reason == "length"
        ? AiFinishReason::TokenLimit
        : (done_reason == "stop" ? AiFinishReason::Eos : AiFinishReason::Unknown);
    return result;
}

AiBackendResult AiBackend::RunOpenAiCompatible(
    const AiPromptRequest& request,
    const std::string& model,
    const std::atomic<bool>* cancel_requested,
    ProgressCallback progress,
    RemoteCommandControl* command_control) const {
    Json messages = Json::array();
    if (settings_.managed_local_server) {
        // Gemma's native chat template rejects a separate system role. Keep the
        // instruction and source text in one user turn for managed local models.
        messages.push_back(Json{
            {"role", "user"},
            {"content", BuildAiSystemPrompt(request) + "\n\n" +
                BuildAiUserPrompt(request)},
        });
    } else {
        messages.push_back(Json{
            {"role", "system"}, {"content", BuildAiSystemPrompt(request)}});
        messages.push_back(Json{
            {"role", "user"}, {"content", BuildAiUserPrompt(request)}});
    }

    Json body = {
        {"model", model},
        {"temperature", 0.2},
        {"max_tokens", settings_.max_output_tokens > 0
            ? settings_.max_output_tokens
            : DefaultMaxOutputTokens(request)},
        {"stream", true},
        {"stop", Json::array({
            "[end of text]", "<|end_of_text|>", "<|eot_id|>",
            "<end_of_turn>", "</s>"})},
        {"messages", std::move(messages)},
    };
    if (settings_.managed_local_server) {
        body["stream_options"] = Json{{"include_usage", true}};
    }

    const auto request_started = std::chrono::steady_clock::now();
    bool first_token_seen = false;
    std::string line_buffer;
    std::string generated;
    std::string finish_reason;
    std::string stop_type;
    AiBackendResult result;
    result.provider = AiProvider::OpenAiCompatible;
    auto last_progress_at = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto consume_line = [&](std::string line) {
        const OpenAiStreamEvent event = ParseOpenAiStreamEvent(std::move(line));
        if (!event.finish_reason.empty()) {
            finish_reason = event.finish_reason;
        }
        if (!event.stop_type.empty()) {
            stop_type = event.stop_type;
        }
        result.prompt_tokens = std::max(result.prompt_tokens, event.prompt_tokens);
        result.generated_tokens = std::max(result.generated_tokens, event.generated_tokens);
        result.prompt_ms = std::max(result.prompt_ms, event.prompt_ms);
        result.generation_ms = std::max(result.generation_ms, event.generation_ms);
        result.tokens_per_second = std::max(result.tokens_per_second, event.tokens_per_second);
        if (event.content.empty()) {
            return;
        }
        if (!first_token_seen) {
            first_token_seen = true;
            result.time_to_first_token_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - request_started).count();
        }
        generated += event.content;
        const auto now = std::chrono::steady_clock::now();
        if (progress && now - last_progress_at >= std::chrono::milliseconds(125)) {
            last_progress_at = now;
            progress(NormalizeGeneratedTextImpl(generated));
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
    if (IsCancelled(cancel_requested) ||
        (command_control && command_control->StopRequested())) {
        return StoppedResult(AiProvider::OpenAiCompatible);
    }
    if (!response.ok) {
        result.error = response.error.empty()
            ? "OpenAI-compatible request failed."
            : response.error;
        result.finish_reason = result.error.find("timed out") != std::string::npos
            ? AiFinishReason::Timeout
            : AiFinishReason::Error;
        return result;
    }
    if (generated.empty()) {
        std::string error;
        const Json root = ParseJsonBody(response, error);
        if (error.empty()) {
            const auto choices = root.find("choices");
            if (choices != root.end() && choices->is_array() && !choices->empty() &&
                (*choices)[0].is_object()) {
                const Json& choice = (*choices)[0];
                const auto message = choice.find("message");
                generated = message != choice.end() && message->is_object()
                    ? JsonString(*message, "content")
                    : std::string{};
                finish_reason = JsonString(choice, "finish_reason");
            }
        }
    }
    generated = NormalizeGeneratedTextImpl(std::move(generated));
    if (generated.empty()) {
        result.error = "OpenAI-compatible server returned an empty response.";
        result.finish_reason = AiFinishReason::Error;
        return result;
    }
    result.success = true;
    result.text = std::move(generated);
    result.finish_reason = FinishReasonFromOpenAi(finish_reason, stop_type);
    if (result.finish_reason == AiFinishReason::Unknown) {
        result.finish_reason = AiFinishReason::Eos;
    }
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
    if (IsCancelled(cancel_requested)) {
        return StoppedResult(AiProvider::LocalLlamaCpp);
    }

    LocalLlamaServerManager& manager = LocalLlamaServerManager::Instance();
    const bool server_was_ready = manager.IsReadyFor(filename);
    std::string start_error;
    if (!manager.EnsureRunning(
            filename,
            settings_.local_threads > 0 ? settings_.local_threads : DefaultLocalThreadCount(),
            120,
            start_error,
            cancel_requested)) {
        if (IsCancelled(cancel_requested)) {
            return StoppedResult(AiProvider::LocalLlamaCpp);
        }
        return ErrorResult(
            AiProvider::LocalLlamaCpp,
            start_error.empty() ? "Could not start llama-server." : start_error);
    }
    if (IsCancelled(cancel_requested)) {
        return StoppedResult(AiProvider::LocalLlamaCpp);
    }

    AiBackendSettings server_settings = settings_;
    server_settings.provider = AiProvider::OpenAiCompatible;
    server_settings.server_url = manager.BaseUrl();
    server_settings.selected_model_key = "openai:" + filename;
    server_settings.managed_local_server = true;
    AiBackend server_backend(std::move(server_settings));
    AiBackendResult result = server_backend.RunOpenAiCompatible(
        request, filename, cancel_requested, std::move(progress), command_control);
    result.provider = AiProvider::LocalLlamaCpp;
    if (result.finish_reason == AiFinishReason::Cancelled &&
        !manager.WaitUntilIdle(2000)) {
        result.error =
            "Cancellation requested, but llama-server is still finishing the task. "
            "Use Unload Model to force-stop the local model.";
    }
    if (!server_was_ready) {
        result.model_load_ms = manager.Snapshot().load_ms;
    }
    if (!result.success && result.finish_reason == AiFinishReason::Timeout) {
        result.error += " The local model remains loaded; retry or use Cancel Task.";
    }
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
