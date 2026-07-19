#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "ai/ai_prompts.hpp"

namespace textlt {

class RemoteCommandControl;

enum class AiProvider {
    Auto,
    Ollama,
    OpenAiCompatible,
    LocalLlamaCpp,
};

enum class AiFinishReason {
    Unknown,
    Eos,
    StopMarker,
    TokenLimit,
    Timeout,
    Cancelled,
    Error,
};

enum class AiModelSource {
    Server,
    ManagedLocal,
};

struct AiBackendSettings {
    std::string server_url;
    AiProvider provider = AiProvider::Auto;
    std::string selected_model_key;
    int timeout_seconds = 120;
    int max_output_tokens = 0;
    int local_threads = 0;
    bool managed_local_server = false;
};

struct AiModelInfo {
    std::string key;
    std::string id;
    std::string title;
    std::string provider_label;
    std::string filename;
    AiModelSource source = AiModelSource::Server;
    bool available = false;
    bool downloaded = false;
    bool gpu_required = false;
    int recommended_vram_mb = 0;
    std::string tier;
};

struct AiBackendResult {
    bool success = false;
    AiProvider provider = AiProvider::Auto;
    std::string text;
    std::string error;
    AiFinishReason finish_reason = AiFinishReason::Unknown;
    int prompt_tokens = 0;
    int generated_tokens = 0;
    double model_load_ms = 0.0;
    double time_to_first_token_ms = 0.0;
    double prompt_ms = 0.0;
    double generation_ms = 0.0;
    double tokens_per_second = 0.0;
};

struct AiConnectionResult {
    bool success = false;
    AiProvider provider = AiProvider::Auto;
    std::string provider_label;
    std::string error;
    std::vector<AiModelInfo> models;
};

class AiBackend {
public:
    using ProgressCallback = std::function<void(const std::string& generated_text)>;

    explicit AiBackend(AiBackendSettings settings);

    AiConnectionResult CheckConnectionAndListModels(
        const std::atomic<bool>* cancel_requested = nullptr,
        RemoteCommandControl* command_control = nullptr) const;
    AiConnectionResult CheckSelectedModelReady(
        const std::atomic<bool>* cancel_requested = nullptr,
        RemoteCommandControl* command_control = nullptr) const;
    AiBackendResult Run(
        const AiPromptRequest& request,
        const std::atomic<bool>* cancel_requested = nullptr,
        ProgressCallback progress = {},
        RemoteCommandControl* command_control = nullptr) const;

    static AiProvider ProviderFromConfig(const std::string& value);
    static std::string ProviderToConfig(AiProvider provider);
    static std::string ProviderLabel(AiProvider provider);
    static std::string NormalizeServerUrl(std::string url);
    static bool IsSupportedServerUrl(const std::string& url);
    static std::string ModelIdFromKey(const std::string& key);
    static std::string NormalizeGeneratedText(std::string text);
    static int RecommendedMaxOutputTokens(const AiPromptRequest& request);
    static std::string FinishReasonLabel(AiFinishReason reason);

private:
    AiConnectionResult TryOllama(
        const std::atomic<bool>* cancel_requested,
        RemoteCommandControl* command_control) const;
    AiConnectionResult TryOpenAiCompatible(
        const std::atomic<bool>* cancel_requested,
        RemoteCommandControl* command_control) const;
    AiBackendResult RunOllama(
        const AiPromptRequest& request, const std::string& model,
        const std::atomic<bool>* cancel_requested, ProgressCallback progress,
        RemoteCommandControl* command_control) const;
    AiBackendResult RunOpenAiCompatible(
        const AiPromptRequest& request,
        const std::string& model,
        const std::atomic<bool>* cancel_requested,
        ProgressCallback progress,
        RemoteCommandControl* command_control) const;
    AiBackendResult RunLocalLlamaCpp(
        const AiPromptRequest& request,
        const std::string& filename,
        const std::atomic<bool>* cancel_requested,
        ProgressCallback progress,
        RemoteCommandControl* command_control) const;

    AiBackendSettings settings_;
};

} // namespace textlt
