#include <cassert>

#include "ai/ai_backend.hpp"

int main() {
    using textlt::AiBackend;
    using textlt::AiProvider;

    assert(AiBackend::NormalizeServerUrl(" http://127.0.0.1:11434/api/ ") ==
           "http://127.0.0.1:11434");
    assert(AiBackend::NormalizeServerUrl("http://localhost:1234/v1") ==
           "http://localhost:1234");
    assert(AiBackend::IsSupportedServerUrl("http://127.0.0.1:11434"));
    assert(AiBackend::IsSupportedServerUrl("https://example.test/v1"));
    assert(!AiBackend::IsSupportedServerUrl("file:///tmp/server"));
    assert(AiBackend::ProviderFromConfig("ollama") == AiProvider::Ollama);
    assert(AiBackend::ProviderFromConfig("openai") == AiProvider::OpenAiCompatible);
    assert(AiBackend::ProviderFromConfig("llama_cpp") == AiProvider::LocalLlamaCpp);
    assert(AiBackend::ProviderFromConfig("unknown") == AiProvider::Auto);
    assert(AiBackend::ModelIdFromKey("ollama:gemma3:4b") == "gemma3:4b");
    assert(AiBackend::ModelIdFromKey("local:model.gguf") == "model.gguf");

    textlt::AiPromptRequest request;
    request.text = "text";
    const textlt::AiBackend invalid_local({
        "", AiProvider::LocalLlamaCpp, "local:../model.gguf", 5});
    const textlt::AiBackendResult invalid_result = invalid_local.Run(request);
    assert(!invalid_result.success);
    assert(invalid_result.error.find("filename is invalid") != std::string::npos);
    return 0;
}
