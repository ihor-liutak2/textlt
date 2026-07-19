#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "ai/ai_backend.hpp"
#include "remote/remote_command_runner.hpp"

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

#ifndef _WIN32
    const auto test_id = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path test_root =
        std::filesystem::temp_directory_path() /
        ("textlt-ai-backend-test-" + std::to_string(test_id));
    std::filesystem::remove_all(test_root);
    const std::filesystem::path runtime_dir =
        test_root / "textlt" / "ai" / "runtimes" / "llama_cpp";
    const std::filesystem::path models_dir = test_root / "textlt" / "ai" / "models";
    std::filesystem::create_directories(runtime_dir);
    std::filesystem::create_directories(models_dir);
    const std::filesystem::path runtime = runtime_dir / "llama-cli";
    const std::filesystem::path model = models_dir / "test.gguf";
    const std::filesystem::path args_file = test_root / "args.txt";
    std::ofstream(model).put('\n');
    {
        std::ofstream script(runtime);
        script << "#!/bin/sh\n"
               << "printf '%s\\n' \"$@\" > \"$TEXTLT_TEST_ARGS\"\n"
               << "printf 'Corrected text.'\n";
    }
    chmod(runtime.c_str(), 0755);
    setenv("XDG_DATA_HOME", test_root.c_str(), 1);
    setenv("TEXTLT_TEST_ARGS", args_file.c_str(), 1);

    textlt::AiBackendSettings local_settings;
    local_settings.provider = AiProvider::LocalLlamaCpp;
    local_settings.selected_model_key = "local:test.gguf";
    local_settings.timeout_seconds = 5;
    local_settings.max_output_tokens = 128;
    local_settings.local_threads = 3;
    const textlt::AiBackendResult local_result =
        textlt::AiBackend(local_settings).Run(request);
    assert(local_result.success);
    assert(local_result.text == "Corrected text.");
    std::ifstream args_input(args_file);
    const std::string args_text(
        (std::istreambuf_iterator<char>(args_input)),
        std::istreambuf_iterator<char>());
    assert(args_text.find("--no-conversation") != std::string::npos);
    assert(args_text.find("--n-predict\n128") != std::string::npos);
    assert(args_text.find("--threads\n3") != std::string::npos);
    assert(args_text.find("--threads-batch\n3") != std::string::npos);
    assert(args_text.find("--prio\n-1") != std::string::npos);
    assert(args_text.find("--poll\n0") != std::string::npos);

    {
        std::ofstream script(runtime, std::ios::trunc);
        script << "#!/bin/sh\n"
               << "printf 'First response.'\n"
               << "sleep 10\n";
    }
    chmod(runtime.c_str(), 0755);
    local_settings.timeout_seconds = 30;
    textlt::RemoteCommandControl command_control;
    std::atomic<bool> first_output_seen{false};
    textlt::AiBackendResult stopped_backend_result;
    const auto stop_started = std::chrono::steady_clock::now();
    std::thread backend_thread([&] {
        stopped_backend_result = textlt::AiBackend(local_settings).Run(
            request,
            nullptr,
            [&](const std::string& generated) {
                if (generated.find("First response.") != std::string::npos) {
                    first_output_seen.store(true);
                }
            },
            &command_control);
    });
    const auto first_output_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!first_output_seen.load() &&
           std::chrono::steady_clock::now() < first_output_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(first_output_seen.load());
    command_control.RequestStop();
    backend_thread.join();
    assert(!stopped_backend_result.success);
    assert(stopped_backend_result.error.find("stopped") != std::string::npos);
    assert(std::chrono::steady_clock::now() - stop_started < std::chrono::seconds(3));

    {
        std::ofstream script(runtime, std::ios::trunc);
        script << "#!/bin/sh\n"
               << "sleep 10\n";
    }
    chmod(runtime.c_str(), 0755);
    local_settings.timeout_seconds = 1;
    const auto timeout_started = std::chrono::steady_clock::now();
    const textlt::AiBackendResult local_timeout =
        textlt::AiBackend(local_settings).Run(request);
    assert(!local_timeout.success);
    assert(local_timeout.error.find("timed out") != std::string::npos);
    assert(std::chrono::steady_clock::now() - timeout_started < std::chrono::seconds(3));

    std::filesystem::remove_all(test_root);
#endif
    return 0;
}
