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
};

struct AiBackendResult {
    bool success = false;
    AiProvider provider = AiProvider::Auto;
    std::string text;
    std::string error;
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
