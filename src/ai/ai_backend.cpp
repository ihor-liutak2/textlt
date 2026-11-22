#include "ai/ai_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <random>
#include <sstream>
#include <utility>

#include "json_utils.hpp"
#include "remote/remote_command_runner.hpp"
#include "remote/remote_http_client.hpp"

namespace textlt {
namespace {

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
    return base + path;
}

std::string CompactErrorText(std::string value) {
    std::string result;
    bool previous_space = false;
    for (unsigned char ch : value) {
        const bool is_space = std::isspace(ch) != 0;
        if (is_space) {
            if (!result.empty() && !previous_space) {
                result.push_back(' ');
            }
        } else {
            result.push_back(static_cast<char>(ch));
        }
        previous_space = is_space;
    }
    return Trim(std::move(result));
}

std::string JsonErrorDetail(const Json& root) {
    if (!root.is_object()) {
        return {};
    }
    const auto error = root.find("error");
    if (error != root.end()) {
        if (error->is_string()) {
            return error->get<std::string>();
        }
        if (error->is_object()) {
            const std::string message = JsonString(*error, "message");
            if (!message.empty()) {
                return message;
            }
            const std::string detail = JsonString(*error, "detail");
            if (!detail.empty()) {
                return detail;
            }
            const std::string type = JsonString(*error, "type");
            if (!type.empty()) {
                return type;
            }
        }
    }
    const std::string message = JsonString(root, "message");
    if (!message.empty()) {
        return message;
    }
    return JsonString(root, "detail");
}

std::string HttpFailureMessage(
    const RemoteHttpResponse& response,
    const std::string& fallback) {
    std::string detail;
    if (!response.body.empty()) {
        const Json root = Json::parse(response.body, nullptr, false);
        if (!root.is_discarded()) {
            detail = JsonErrorDetail(root);
        }
    }
    if (detail.empty()) {
        detail = response.error;
    }
    detail = CompactErrorText(std::move(detail));
    if (response.status_code > 0) {
        std::string message = "AI server returned HTTP " +
            std::to_string(response.status_code);
        if (!detail.empty()) {
            message += ": " + detail;
        } else {
            message += ".";
        }
        return message;
    }
    return detail.empty() ? fallback : detail;
}

Json ParseJsonBody(const RemoteHttpResponse& response, std::string& error) {
    if (!response.ok) {
        error = HttpFailureMessage(response, "AI server request failed.");
        return Json();
    }
    Json root = Json::parse(response.body, nullptr, false);
    if (root.is_discarded()) {
        error = "AI server returned invalid JSON.";
        return Json();
    }
    return root;
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
    std::string error;
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
    event.error = JsonString(root, "error");
    event.done = JsonBool(root, "done", false);
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
    bool done = false;
    std::string error;
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
    if (line.empty()) {
        return event;
    }
    if (line == "[DONE]") {
        event.done = true;
        return event;
    }
    const Json root = Json::parse(line, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return event;
    }
    event.error = root.contains("error") ? JsonErrorDetail(root) : std::string{};
    const std::string type = JsonString(root, "type");
    if (type == "content_block_delta") {
        const auto delta = root.find("delta");
        if (delta != root.end() && JsonString(*delta, "type") == "text_delta") {
            event.content = JsonString(*delta, "text");
        }
    } else if (type == "message_stop" || type == "response.completed") {
        event.done = true;
    } else if (type == "message_delta") {
        const auto delta = root.find("delta");
        if (delta != root.end()) {
            const std::string reason = JsonString(*delta, "stop_reason");
            event.finish_reason = reason == "max_tokens" ? "length" : reason;
        }
    } else if (type == "response.output_text.delta") {
        event.content = JsonString(root, "delta");
    } else if (type == "response.incomplete" || type == "response.failed") {
        const auto response = root.find("response");
        if (response != root.end()) {
            event.error = JsonErrorDetail(*response);
            const auto details = response->find("incomplete_details");
            if (details != response->end() && JsonString(*details, "reason") == "max_output_tokens") {
                event.finish_reason = "length";
            }
        }
        if (event.error.empty() && event.finish_reason.empty()) {
            event.error = "OpenCode did not complete the response.";
        }
    } else if (type == "error" && event.error.empty()) {
        event.error = JsonErrorDetail(root);
        if (event.error.empty()) {
            event.error = "The provider reported a streaming error.";
        }
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
        event.prompt_tokens = std::max(
            JsonInteger(*usage, "prompt_tokens"), JsonInteger(*usage, "input_tokens"));
        event.generated_tokens = std::max(
            JsonInteger(*usage, "completion_tokens"), JsonInteger(*usage, "output_tokens"));
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
    return std::clamp(estimated_input_tokens + estimated_input_tokens / 2 + 24, 64, 2048);
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
    if (Trim(settings_.server_url).empty()) {
        settings_.server_url = DefaultEndpoint(settings_.provider, settings_.opencode_use_go);
    }
    if (settings_.provider != AiProvider::ChatGpt) {
        settings_.server_url = NormalizeServerUrl(settings_.server_url);
    }
    settings_.api_key = Trim(std::move(settings_.api_key));
    if (settings_.timeout_seconds <= 0) {
        settings_.timeout_seconds = 120;
    }
}

std::string AiBackend::DefaultEndpoint(AiProvider provider, bool opencode_use_go) {
    switch (provider) {
        case AiProvider::Ollama: return "http://localhost:11434/v1";
        case AiProvider::OpenCodeGo: return opencode_use_go
            ? "https://opencode.ai/inference/go" : "https://opencode.ai/inference";
        case AiProvider::ChatGpt: return "codex";
        case AiProvider::Claude: return "https://api.anthropic.com";
        case AiProvider::Mimo: return "https://api.xiaomimimo.com/v1";
        default: return {};
    }
}

std::string AiBackend::ValidationError(const AiBackendSettings& settings) {
    if (settings.provider == AiProvider::ChatGpt) {
        const auto& effort = settings.effort;
        if (!effort.empty() && effort != "none" && effort != "minimal" &&
            effort != "low" && effort != "medium" && effort != "high" &&
            effort != "xhigh" && effort != "max") {
            return "Choose an effort level supported by the selected Codex model.";
        }
        return {};
    }
    const std::string url = NormalizeServerUrl(settings.server_url.empty()
        ? DefaultEndpoint(settings.provider, settings.opencode_use_go) : settings.server_url);
    if (!IsSupportedServerUrl(url)) {
        return "Enter a valid HTTP or HTTPS server URL.";
    }
    if (settings.provider == AiProvider::OpenCodeGo &&
        url == DefaultEndpoint(settings.provider, !settings.opencode_use_go)) {
        return "The OpenCode URL does not match the selected billing plan.";
    }
    if (settings.provider == AiProvider::Claude || settings.provider == AiProvider::Mimo ||
        settings.provider == AiProvider::OpenCodeGo) {
        if (url.rfind("https://", 0) != 0) {
            return "This provider requires an HTTPS URL.";
        }
        const std::string key = Trim(settings.api_key);
        if (key.empty()) {
            return "Enter an API key for " + ProviderLabel(settings.provider) + ".";
        }
        if (std::any_of(key.begin(), key.end(), [](unsigned char ch) {
                return ch <= ' ' || ch == 127;
            })) {
            return "The API key contains whitespace or a control character.";
        }
    }
    const auto& effort = settings.effort;
    if (!effort.empty()) {
        const auto is_one_of = [&](std::initializer_list<const char*> values) {
            return std::any_of(values.begin(), values.end(),
                [&](const char* value) { return effort == value; });
        };
        if ((settings.provider == AiProvider::Mimo && !is_one_of({"enabled", "disabled"})) ||
            (settings.provider == AiProvider::Claude && !is_one_of({"low", "medium", "high", "xhigh", "max"})) ||
            (settings.provider == AiProvider::OpenCodeGo && !is_one_of({"none", "minimal", "low", "medium", "high", "xhigh", "max"}))) {
            return "This provider does not accept the entered effort value.";
        }
    }
    return {};
}

std::vector<std::string> AiBackend::HttpHeaders(bool stream) const {
    std::vector<std::string> headers{
        stream ? "Accept: text/event-stream" : "Accept: application/json",
    };
    if (stream) {
        headers.emplace_back("Content-Type: application/json");
    }
    if (settings_.provider == AiProvider::Claude) {
        headers.emplace_back("anthropic-version: 2023-06-01");
        headers.push_back("x-api-key: " + settings_.api_key);
    } else if (settings_.provider == AiProvider::Mimo) {
        headers.push_back("api-key: " + settings_.api_key);
    } else if (settings_.provider == AiProvider::OpenCodeGo) {
        headers.push_back("Authorization: Bearer " + settings_.api_key);
        if (stream) {
            std::random_device random;
            std::ostringstream session;
            session << "x-opencode-session: ses_" << std::hex << random() << random();
            headers.push_back(session.str());
        }
    }
    return headers;
}

std::string AiBackend::Redact(std::string message) const {
    if (!settings_.api_key.empty()) {
        size_t pos = 0;
        while ((pos = message.find(settings_.api_key, pos)) != std::string::npos) {
            message.replace(pos, settings_.api_key.size(), "[redacted]");
            pos += sizeof("[redacted]") - 1;
        }
    }
    return message;
}

AiProvider AiBackend::ProviderFromConfig(const std::string& value) {
    if (value == "ollama") {
        return AiProvider::Ollama;
    }
    if (value == "openai") {
        return AiProvider::OpenAiCompatible;
    }
    if (value == "opencode") return AiProvider::OpenCodeGo;
    if (value == "chatgpt") return AiProvider::ChatGpt;
    if (value == "claude") return AiProvider::Claude;
    if (value == "mimo") return AiProvider::Mimo;
    return AiProvider::Auto;
}

std::string AiBackend::ProviderToConfig(AiProvider provider) {
    switch (provider) {
        case AiProvider::Ollama:
            return "ollama";
        case AiProvider::OpenAiCompatible:
            return "openai";
        case AiProvider::OpenCodeGo: return "opencode";
        case AiProvider::ChatGpt: return "chatgpt";
        case AiProvider::Claude: return "claude";
        case AiProvider::Mimo: return "mimo";
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
        case AiProvider::OpenCodeGo: return "OpenCode";
        case AiProvider::ChatGpt: return "ChatGPT";
        case AiProvider::Claude: return "Claude";
        case AiProvider::Mimo: return "MiMo";
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
    for (const std::string suffix : {"/v1/chat/completions", "/v1/messages", "/api/chat"}) {
        if (url.size() > suffix.size() && url.compare(url.size() - suffix.size(),
                suffix.size(), suffix) == 0) {
            url.erase(url.size() - suffix.size());
            return url;
        }
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
    const size_t offset = normalized.rfind("https://", 0) == 0 ? 8 :
        normalized.rfind("http://", 0) == 0 ? 7 : 0;
    return offset > 0 && normalized.size() > offset && normalized[offset] != '/' &&
        normalized.find_first_of("?#@\\") == std::string::npos &&
        std::none_of(normalized.begin(), normalized.end(),
            [](unsigned char ch) { return ch <= ' ' || ch == 127; });
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
        command_control, false);
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
            true,
            "Model reported by the configured Ollama server.",
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
    result.provider = settings_.provider == AiProvider::Auto
        ? AiProvider::OpenAiCompatible : settings_.provider;
    result.provider_label = ProviderLabel(result.provider);

    RemoteHttpClient client;
    const char* path = result.provider == AiProvider::Claude ? "/v1/models?limit=1000" : "/v1/models";
    const RemoteHttpResponse response = client.RequestStreaming(
        "GET", JoinUrl(settings_.server_url, path), HttpHeaders(false), {},
        std::min(settings_.timeout_seconds, 20), cancel_requested, {}, command_control, false);
    if (IsCancelled(cancel_requested)) {
        result.error = "Operation stopped.";
        return result;
    }
    const Json root = ParseJsonBody(response, result.error);
    if (!result.error.empty()) {
        result.error = Redact(std::move(result.error));
        return result;
    }
    const auto data = root.find("data");
    if (data == root.end() || !data->is_array()) {
        result.error = result.provider_label + " returned an invalid model list.";
        return result;
    }
    for (const Json& item : *data) {
        const std::string id = JsonString(item, "id");
        const std::string kind = JsonString(item, "type");
        if (id.empty() || (result.provider == AiProvider::Mimo &&
            (id.find("-tts") != std::string::npos || id.find("-asr") != std::string::npos)) ||
            (result.provider == AiProvider::OpenCodeGo && !kind.empty() && kind != "chat")) {
            continue;
        }
        AiModelInfo model{
            ProviderToConfig(result.provider) + ":" + id, id,
            JsonString(item, "display_name", JsonString(item, "name", id)),
            result.provider_label, true, {}, {},
        };
        if (result.provider == AiProvider::OpenCodeGo) {
            const auto capabilities = item.find("capabilities");
            if (capabilities != item.end() && capabilities->is_object()) {
                const auto reasoning = capabilities->find("reasoning");
                if (reasoning != capabilities->end() && reasoning->is_object()) {
                    const auto efforts = reasoning->find("effort");
                    if (efforts != reasoning->end() && efforts->is_array()) {
                        for (const auto& effort : *efforts) {
                            if (effort.is_string()) {
                                model.efforts.push_back(effort.get<std::string>());
                            }
                        }
                    }
                }
            }
        }
        result.models.push_back(std::move(model));
    }
    result.success = true;
    return result;
}

AiConnectionResult AiBackend::CheckConnectionAndListModels(
    const std::atomic<bool>* cancel_requested,
    RemoteCommandControl* command_control) const {
    AiConnectionResult result;
    result.provider = settings_.provider;
    result.provider_label = ProviderLabel(result.provider);
    result.error = IsCancelled(cancel_requested) ? "Operation stopped." : ValidationError(settings_);
    if (!result.error.empty()) {
        return result;
    }
    if (settings_.provider == AiProvider::ChatGpt) {
        AiBackendResult unused;
        return RunCodex(nullptr, unused, cancel_requested, {}, command_control);
    }
    if (settings_.provider == AiProvider::Ollama || settings_.provider == AiProvider::Auto) {
        result = TryOllama(cancel_requested, command_control);
        if (settings_.provider == AiProvider::Ollama || result.success || IsCancelled(cancel_requested)) {
            return result;
        }
    }
    return TryOpenAiCompatible(cancel_requested, command_control);
}

AiConnectionResult AiBackend::GetOllamaModelEfforts(
    const std::atomic<bool>* cancel_requested,
    RemoteCommandControl* command_control) const {
    AiConnectionResult result;
    result.provider = AiProvider::Ollama;
    result.provider_label = ProviderLabel(result.provider);
    result.error = ValidationError(settings_);
    if (!result.error.empty()) {
        return result;
    }
    if (settings_.model.empty()) {
        result.error = "Choose an Ollama model first.";
        return result;
    }
    const auto response = RemoteHttpClient().RequestStreaming(
        "POST", JoinUrl(settings_.server_url, "/api/show"),
        {"Accept: application/json", "Content-Type: application/json"},
        Json{{"model", settings_.model}}.dump(), std::min(settings_.timeout_seconds, 20),
        cancel_requested, {}, command_control, false);
    const Json root = ParseJsonBody(response, result.error);
    if (!result.error.empty()) {
        return result;
    }

    AiModelInfo model{"ollama:" + settings_.model, settings_.model, settings_.model,
        result.provider_label, true, {}, {}};
    const auto thinking = root.find("thinking");
    if (thinking != root.end() && thinking->is_object()) {
        const auto values = thinking->find("values");
        if (values != thinking->end() && values->is_array()) {
            for (const auto& value : *values) {
                if (value.is_boolean() || value.is_string()) {
                    model.efforts.push_back(value.is_boolean() ? value.dump() : value.get<std::string>());
                }
            }
        }
    }
    result.models.push_back(std::move(model));
    result.success = true;
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
            Json{{"role", "system"}, {"content", request.system}},
            Json{{"role", "user"}, {"content", request.text}},
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

    if (!settings_.effort.empty()) {
        if (settings_.effort == "true" || settings_.effort == "false") {
            body["think"] = settings_.effort == "true";
        } else {
            body["think"] = settings_.effort;
        }
    }

    const auto request_started = std::chrono::steady_clock::now();
    bool first_token_seen = false;
    std::string line_buffer;
    std::string generated;
    std::string done_reason;
    bool stream_completed = false;
    AiBackendResult result;
    result.provider = AiProvider::Ollama;
    auto last_progress_at = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto consume_line = [&](std::string line) {
        const OllamaStreamEvent event = ParseOllamaStreamEvent(line);
        if (!event.error.empty()) {
            result.error = event.error;
        }
        result.prompt_tokens = std::max(result.prompt_tokens, event.prompt_tokens);
        result.generated_tokens = std::max(result.generated_tokens, event.generated_tokens);
        result.prompt_ms = std::max(result.prompt_ms, event.prompt_ms);
        result.generation_ms = std::max(result.generation_ms, event.generation_ms);
        result.tokens_per_second = std::max(result.tokens_per_second, event.tokens_per_second);
        if (event.done) {
            stream_completed = true;
            if (!event.done_reason.empty()) {
                done_reason = event.done_reason;
            }
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
        command_control, false);
    if (!line_buffer.empty()) {
        consume_line(std::move(line_buffer));
    }
    if (IsCancelled(cancel_requested) ||
        (command_control && command_control->StopRequested())) {
        return StoppedResult(AiProvider::Ollama);
    }
    if (!result.error.empty()) {
        result.finish_reason = AiFinishReason::Error;
        return result;
    }
    if (!response.ok) {
        result.error = HttpFailureMessage(response, "Ollama request failed.");
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
    if (!stream_completed) {
        return ErrorResult(AiProvider::Ollama, "Ollama closed the response stream early.");
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
    const auto provider = settings_.provider == AiProvider::Auto
        ? AiProvider::OpenAiCompatible : settings_.provider;
    const std::string system = request.system;
    const std::string user = request.text;
    const int tokens = settings_.max_output_tokens > 0
        ? settings_.max_output_tokens : DefaultMaxOutputTokens(request);
    Json body = {{"model", model}, {"stream", true}};
    std::string path = "/v1/chat/completions";
    if (provider == AiProvider::Claude) {
        path = "/v1/messages";
        body["system"] = system;
        body["messages"] = Json::array({Json{{"role", "user"}, {"content", user}}});
        body["max_tokens"] = std::max(tokens, 4096);
        if (!settings_.effort.empty()) {
            body["output_config"] = {{"effort", settings_.effort}};
        }
    } else if (provider == AiProvider::OpenCodeGo) {
        path = "/openai/v1/responses";
        body["instructions"] = system;
        body["input"] = user;
        body["max_output_tokens"] = std::max(tokens, 4096);
        if (!settings_.effort.empty()) {
            body["reasoning"] = {{"effort", settings_.effort}};
        }
    } else {
        body["messages"] = Json::array({
            Json{{"role", "system"}, {"content", system}},
            Json{{"role", "user"}, {"content", user}},
        });
        if (provider == AiProvider::Mimo) {
            body["max_completion_tokens"] = settings_.effort == "disabled"
                ? tokens : std::max(tokens, 4096);
            if (!settings_.effort.empty()) {
                body["thinking"] = {{"type", settings_.effort}};
            }
        } else {
            body["max_tokens"] = tokens;
            body["temperature"] = 0.2;
            body["stop"] = Json::array({"[end of text]", "<|end_of_text|>", "<|eot_id|>",
                "<end_of_turn>", "</s>"});
        }
    }
    const auto request_started = std::chrono::steady_clock::now();
    bool first_token_seen = false;
    std::string line_buffer;
    std::string generated;
    std::string finish_reason;
    std::string stop_type;
    bool stream_completed = false;
    AiBackendResult result;
    result.provider = provider;
    auto last_progress_at = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto consume_line = [&](std::string line) {
        const OpenAiStreamEvent event = ParseOpenAiStreamEvent(std::move(line));
        if (!event.error.empty()) {
            result.error = Redact(event.error);
        }
        if (event.done || (provider != AiProvider::Claude && !event.finish_reason.empty())) {
            stream_completed = true;
        }
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
        JoinUrl(settings_.server_url, path),
        HttpHeaders(true),
        body.dump(),
        settings_.timeout_seconds,
        cancel_requested,
        [&](const std::string& chunk) {
            ConsumeCompleteLines(line_buffer, chunk, consume_line);
        },
        command_control, false);
    if (!line_buffer.empty()) {
        consume_line(std::move(line_buffer));
    }
    if (IsCancelled(cancel_requested) ||
        (command_control && command_control->StopRequested())) {
        return StoppedResult(provider);
    }
    if (!result.error.empty()) {
        result.finish_reason = AiFinishReason::Error;
        return result;
    }
    if (!response.ok && (provider != AiProvider::OpenAiCompatible ||
            !(stream_completed && !generated.empty()))) {
        result.error = Redact(HttpFailureMessage(response, ProviderLabel(provider) + " request failed."));
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
        result.error = ProviderLabel(provider) + " returned an empty response.";
        result.finish_reason = AiFinishReason::Error;
        return result;
    }
    if (provider != AiProvider::OpenAiCompatible && !stream_completed) {
        return ErrorResult(provider, ProviderLabel(provider) + " closed the response stream early.");
    }
    result.success = true;
    result.text = std::move(generated);
    result.finish_reason = FinishReasonFromOpenAi(finish_reason, stop_type);
    if (result.finish_reason == AiFinishReason::Unknown) {
        result.finish_reason = AiFinishReason::Eos;
    }
    return result;
}

AiConnectionResult AiBackend::RunCodex(
    const AiPromptRequest* request,
    AiBackendResult& generation,
    const std::atomic<bool>* cancel_requested,
    ProgressCallback progress,
    RemoteCommandControl* command_control) const {
    AiConnectionResult result;
    result.provider = AiProvider::ChatGpt;
    result.provider_label = ProviderLabel(result.provider);
    generation.provider = result.provider;

    std::string buffer;
    std::string thread_id;
    std::string current_text;
    std::string current_item;
    std::string final_text;
    bool finished = false;
    int model_list_id = 3;
    const auto started = std::chrono::steady_clock::now();
    const auto rpc = [](int id, const char* method, Json params) {
        return Json{{"id", id}, {"method", method}, {"params", std::move(params)}}.dump() + "\n";
    };
    const std::string init = rpc(1, "initialize",
        {{"clientInfo", {{"name", "textlt"}, {"title", "TextLT"}, {"version", "1"}}}});
    const auto handle_output = [&](const std::string& chunk,
                                   const RemoteCommandRunner::OutputCallback& send) {
        const auto finish = [&](std::string error = {}) {
            result.error = std::move(error);
            finished = true;
            send({});
        };
        ConsumeCompleteLines(buffer, chunk, [&](std::string line) {
            if (finished) {
                return;
            }
            const Json message = Json::parse(line, nullptr, false);
            if (!message.is_object()) {
                return;
            }
            if (message.contains("error") && message.contains("id")) {
                finish(JsonErrorDetail(message));
                if (result.error.empty()) {
                    result.error = "Codex app-server rejected the request.";
                }
                return;
            }
            const int id = JsonInteger(message, "id");
            const Json payload = message.value("result", Json::object());
            if (id == 1) {
                send(Json{{"method", "initialized"}}.dump() + "\n" +
                    rpc(2, "account/read", {{"refreshToken", false}}));
            } else if (id == 2) {
                const auto account = payload.find("account");
                if (account == payload.end() || JsonString(*account, "type") != "chatgpt") {
                    finish("Sign in with ChatGPT in Codex (codex login), then try again.");
                } else if (!request) {
                    send(rpc(model_list_id, "model/list", {{"includeHidden", false}}));
                } else {
                    send(rpc(3, "thread/start", {
                        {"model", settings_.model},
                        {"modelProvider", "openai"},
                        {"ephemeral", true},
                        {"approvalPolicy", "never"},
                        {"sandbox", "read-only"},
                        {"baseInstructions", "Follow the developer instructions without using tools."},
                        {"developerInstructions", request->system},
                    }));
                }
            } else if (id == model_list_id && !request) {
                const auto models = payload.find("data");
                if (models == payload.end() || !models->is_array()) {
                    finish("Codex app-server returned an invalid model catalog.");
                    return;
                }
                for (const auto& model : *models) {
                    const std::string name = JsonString(model, "model", JsonString(model, "id"));
                    if (name.empty()) {
                        continue;
                    }
                    AiModelInfo info{"chatgpt:" + name, name, JsonString(model, "displayName", name),
                        result.provider_label, true, JsonString(model, "description"), {}};
                    const auto efforts = model.find("supportedReasoningEfforts");
                    if (efforts != model.end() && efforts->is_array()) {
                        for (const auto& effort : *efforts) {
                            const std::string value = JsonString(effort, "reasoningEffort");
                            if (!value.empty()) {
                                info.efforts.push_back(value);
                            }
                        }
                    }
                    result.models.push_back(std::move(info));
                }
                const std::string cursor = JsonString(payload, "nextCursor");
                if (cursor.empty()) {
                    finish();
                } else {
                    send(rpc(++model_list_id, "model/list", {{"cursor", cursor}, {"includeHidden", false}}));
                }
            } else if (id == 3 && request) {
                const auto thread = payload.find("thread");
                if (thread != payload.end()) {
                    thread_id = JsonString(*thread, "id");
                }
                if (thread_id.empty()) {
                    finish("Codex app-server did not return a thread ID.");
                    return;
                }
                Json turn = {
                    {"threadId", thread_id},
                    {"input", Json::array({Json{{"type", "text"},
                        {"text", request->text}, {"text_elements", Json::array()}}})},
                    {"approvalPolicy", "never"},
                    {"sandboxPolicy", {{"type", "readOnly"}, {"networkAccess", false}}},
                };
                if (!settings_.effort.empty()) {
                    turn["effort"] = settings_.effort;
                }
                send(rpc(4, "turn/start", std::move(turn)));
            } else if (request && !thread_id.empty() && id == 0) {
                const Json params = message.value("params", Json::object());
                if (JsonString(params, "threadId") != thread_id) {
                    return;
                }
                const std::string method = JsonString(message, "method");
                if (method == "item/agentMessage/delta") {
                    const std::string item_id = JsonString(params, "itemId");
                    if (current_item != item_id) {
                        current_item = item_id;
                        current_text.clear();
                    }
                    if (generation.time_to_first_token_ms == 0) {
                        generation.time_to_first_token_ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - started).count();
                    }
                    current_text += JsonString(params, "delta");
                    if (progress) {
                        progress(current_text);
                    }
                } else if (method == "item/completed") {
                    const auto item = params.find("item");
                    if (item != params.end() && JsonString(*item, "type") == "agentMessage" &&
                        JsonString(*item, "phase") != "commentary") {
                        final_text = JsonString(*item, "text");
                    }
                } else if (method == "turn/completed") {
                    const Json turn = params.value("turn", Json::object());
                    if (JsonString(turn, "status") != "completed") {
                        const std::string detail = JsonErrorDetail(turn);
                        finish(detail.empty() ? "The Codex turn failed or was interrupted." : detail);
                        return;
                    }
                    generation.text = NormalizeGeneratedTextImpl(
                        final_text.empty() ? current_text : final_text);
                    generation.success = !generation.text.empty();
                    generation.finish_reason = generation.success ? AiFinishReason::Eos : AiFinishReason::Error;
                    finish(generation.success ? std::string{} : "Codex returned an empty response.");
                }
            }
        });
    };

    RemoteCommandControl local_control;
    const auto process = RemoteCommandRunner().RunStreaming(
        {settings_.server_url, "-c", "model_provider=\"openai\"", "-c", "web_search=\"disabled\"",
            "-c", "features.shell_tool=false", "-c", "features.hooks=false",
            "-c", "features.apps=false", "-c", "features.multi_agent=false",
            "-c", "mcp_servers={}", "-c", "plugins={}", "app-server"},
        init, cancel_requested, {},
        {request ? settings_.timeout_seconds : std::min(settings_.timeout_seconds, 20)},
        command_control ? command_control : &local_control, handle_output);
    if (IsCancelled(cancel_requested) || (process.cancelled && !finished)) {
        result.error = "Operation stopped.";
    } else if (process.timed_out) {
        result.error = "Codex app-server timed out.";
    } else if (!finished) {
        result.error = process.exit_code == 127 || process.exit_code == -1
            ? "Cannot start Codex. Install the Codex CLI or enter its executable path."
            : "Codex app-server closed before responding. Check that the Codex CLI is up to date.";
    }
    result.success = result.error.empty();
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
    if (settings_.model.empty()) {
        return ErrorResult(settings_.provider, "No AI model is selected.");
    }
    const std::string invalid = ValidationError(settings_);
    if (!invalid.empty()) {
        return ErrorResult(settings_.provider, invalid);
    }
    if (settings_.provider == AiProvider::ChatGpt) {
        AiBackendResult result;
        const auto connection = RunCodex(&request, result, cancel_requested, progress, command_control);
        if (!connection.success) {
            return IsCancelled(cancel_requested) ? StoppedResult(settings_.provider)
                : ErrorResult(settings_.provider, connection.error);
        }
        return result;
    }
    AiProvider provider = settings_.provider;
    if (provider == AiProvider::Auto) {
        const auto connection = CheckConnectionAndListModels(cancel_requested, command_control);
        if (!connection.success) {
            return ErrorResult(provider, connection.error);
        }
        provider = connection.provider;
    }
    if (provider == AiProvider::Ollama) {
        return RunOllama(request, settings_.model, cancel_requested, progress, command_control);
    }
    return RunOpenAiCompatible(request, settings_.model, cancel_requested, progress, command_control);
}

} // namespace textlt
