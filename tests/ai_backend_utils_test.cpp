#include <cassert>
#include <string>

#include "ai/ai_backend.hpp"

int main() {
    using textlt::AiBackend;
    using textlt::AiPromptRequest;
    using textlt::AiProvider;

    assert(AiBackend::NormalizeServerUrl(" http://127.0.0.1:11434/api/ ") ==
           "http://127.0.0.1:11434");
    assert(AiBackend::NormalizeServerUrl("http://localhost:1234/v1") ==
           "http://localhost:1234");
    assert(AiBackend::IsSupportedServerUrl("http://127.0.0.1:11434"));
    assert(AiBackend::IsSupportedServerUrl("https://example.test/v1"));
    assert(!AiBackend::IsSupportedServerUrl("file:///tmp/server"));

    assert(AiBackend::ProviderFromConfig("ollama") == AiProvider::Ollama);
    assert(AiBackend::ProviderFromConfig("openai") ==
           AiProvider::OpenAiCompatible);
    assert(AiBackend::ProviderFromConfig("unknown") == AiProvider::Auto);
    assert(AiBackend::ProviderToConfig(AiProvider::Ollama) == "ollama");
    assert(AiBackend::ProviderToConfig(AiProvider::OpenAiCompatible) == "openai");

    assert(AiBackend::ModelIdFromKey("ollama:gemma3:4b") == "gemma3:4b");
    assert(AiBackend::ModelIdFromKey("openai:gpt-test") == "gpt-test");
    assert(AiBackend::NormalizeGeneratedText(
               "Corrected text. [end of text] [end of text]") ==
           "Corrected text.");
    assert(AiBackend::NormalizeGeneratedText("Corrected text.<|eot_id|></s>") ==
           "Corrected text.");
    assert(AiBackend::NormalizeGeneratedText("Corrected text. <end_of_turn>") ==
           "Corrected text.");
    assert(AiBackend::NormalizeGeneratedText("Corrected text. [END OF TEXT]") ==
           "Corrected text.");
    assert(AiBackend::NormalizeGeneratedText(
               "The literal [end of text] label remains inside the sentence.") ==
           "The literal [end of text] label remains inside the sentence.");

    AiPromptRequest request;
    request.text = "text";
    assert(AiBackend::RecommendedMaxOutputTokens(request) >= 64);
    assert(AiBackend::RecommendedMaxOutputTokens(request) < 512);
    AiPromptRequest long_request = request;
    long_request.text = std::string(1200, 'a');
    assert(AiBackend::RecommendedMaxOutputTokens(long_request) >
           AiBackend::RecommendedMaxOutputTokens(request));

    return 0;
}
