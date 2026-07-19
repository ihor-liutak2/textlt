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
    const std::filesystem::path completion_runtime = runtime_dir / "llama-completion";
    const std::filesystem::path cli_runtime = runtime_dir / "llama-cli";
    const std::filesystem::path model = models_dir / "test.gguf";
    const std::filesystem::path args_file = test_root / "args.txt";
    const std::filesystem::path prompt_copy = test_root / "prompt.txt";
    std::ofstream(model).put('\n');
    {
        std::ofstream script(completion_runtime);
        script << R"SCRIPT(#!/bin/sh
printf '%s\n' "$@" > "$TEXTLT_TEST_ARGS"
previous=''
has_jinja=0
has_single_turn=0
has_raw_completion=0
has_unsupported_timing_flag=0
for argument in "$@"; do
  if [ "$previous" = '--file' ]; then
    cp "$argument" "$TEXTLT_TEST_PROMPT"
  fi
  case "$argument" in
    --jinja) has_jinja=1 ;;
    --single-turn) has_single_turn=1 ;;
    -no-cnv|--no-conversation) has_raw_completion=1 ;;
    --no-show-timings) has_unsupported_timing_flag=1 ;;
  esac
  previous="$argument"
done
if [ "$has_jinja" -eq 1 ] && [ "$has_single_turn" -eq 1 ] &&
   [ "$has_raw_completion" -eq 0 ] &&
   [ "$has_unsupported_timing_flag" -eq 0 ]; then
  printf 'Corrected text. [end of text] [end of text]'
fi
)SCRIPT";
    }
    chmod(completion_runtime.c_str(), 0755);
    {
        std::ofstream script(cli_runtime);
        script << "#!/bin/sh\n"
               << "printf 'llama-cli should not be selected when llama-completion exists.'\n";
    }
    chmod(cli_runtime.c_str(), 0755);
    setenv("XDG_DATA_HOME", test_root.c_str(), 1);
    setenv("TEXTLT_TEST_ARGS", args_file.c_str(), 1);
    setenv("TEXTLT_TEST_PROMPT", prompt_copy.c_str(), 1);

    textlt::AiBackendSettings local_settings;
    local_settings.provider = AiProvider::LocalLlamaCpp;
    local_settings.selected_model_key = "local:test.gguf";
    local_settings.timeout_seconds = 5;
    local_settings.max_output_tokens = 128;
    local_settings.local_threads = 3;

    textlt::AiBackendSettings readiness_settings = local_settings;
    readiness_settings.provider = AiProvider::Ollama;
    const textlt::AiConnectionResult ready_local =
        textlt::AiBackend(readiness_settings).CheckSelectedModelReady();
    assert(ready_local.success);
    assert(ready_local.provider == AiProvider::LocalLlamaCpp);
    assert(ready_local.provider_label == "llama.cpp");
    assert(ready_local.models.size() == 1);
    assert(ready_local.models[0].key == "local:test.gguf");

    std::filesystem::remove(model);
    const textlt::AiConnectionResult missing_local =
        textlt::AiBackend(readiness_settings).CheckSelectedModelReady();
    assert(!missing_local.success);
    assert(missing_local.error.find("not downloaded") != std::string::npos);
    std::ofstream(model).put('\n');

    const textlt::AiBackendResult local_result =
        textlt::AiBackend(local_settings).Run(request);
    assert(local_result.success);
    assert(local_result.text == "Corrected text.");
    std::ifstream args_input(args_file);
    const std::string args_text(
        (std::istreambuf_iterator<char>(args_input)),
        std::istreambuf_iterator<char>());
    assert(args_text.find("--jinja") != std::string::npos);
    assert(args_text.find("--single-turn") != std::string::npos);
    assert(args_text.find("-no-cnv") == std::string::npos);
    assert(args_text.find("--no-conversation") == std::string::npos);
    assert(args_text.find("--log-disable") == std::string::npos);
    assert(args_text.find("--no-show-timings") == std::string::npos);
    assert(args_text.find("--color\noff") != std::string::npos);
    assert(args_text.find("--n-predict\n128") != std::string::npos);
    assert(args_text.find("--threads\n3") != std::string::npos);
    assert(args_text.find("--threads-batch\n3") != std::string::npos);
    assert(args_text.find("--prio\n-1") != std::string::npos);
    assert(args_text.find("--poll\n0") != std::string::npos);

    std::ifstream prompt_input(prompt_copy);
    const std::string prompt_text(
        (std::istreambuf_iterator<char>(prompt_input)),
        std::istreambuf_iterator<char>());
    assert(prompt_text.find(
               "Translate the supplied text explicitly from English into Ukrainian.") !=
           std::string::npos);
    assert(prompt_text.find("Text to process:\ntext") != std::string::npos);

    std::filesystem::remove(completion_runtime);
    {
        std::ofstream script(cli_runtime, std::ios::trunc);
        script << "#!/bin/sh\n"
               << "printf '%s\\n' \"$@\" > \"$TEXTLT_TEST_ARGS\"\n"
               << "printf 'Fallback corrected text.'\n";
    }
    chmod(cli_runtime.c_str(), 0755);
    const textlt::AiBackendResult cli_fallback_result =
        textlt::AiBackend(local_settings).Run(request);
    assert(cli_fallback_result.success);
    assert(cli_fallback_result.text == "Fallback corrected text.");
    std::ifstream fallback_args_input(args_file);
    const std::string fallback_args_text(
        (std::istreambuf_iterator<char>(fallback_args_input)),
        std::istreambuf_iterator<char>());
    assert(fallback_args_text.find("--jinja") != std::string::npos);
    assert(fallback_args_text.find("--single-turn") != std::string::npos);
    assert(fallback_args_text.find("-no-cnv") == std::string::npos);
    assert(fallback_args_text.find("--no-conversation") == std::string::npos);

    {
        std::ofstream script(cli_runtime, std::ios::trunc);
        script << "#!/bin/sh\n"
               << "printf '%s\n' '--no-conversation is not supported by llama-cli'\n"
               << "printf '%s\n' 'please use llama-completion instead'\n";
    }
    chmod(cli_runtime.c_str(), 0755);
    const textlt::AiBackendResult incompatible_runtime_result =
        textlt::AiBackend(local_settings).Run(request);
    assert(!incompatible_runtime_result.success);
    assert(incompatible_runtime_result.error.find("llama-completion") != std::string::npos);

    {
        std::ofstream script(cli_runtime, std::ios::trunc);
        script << "#!/bin/sh\n"
               << "printf 'First response.'\n"
               << "sleep 10\n";
    }
    chmod(cli_runtime.c_str(), 0755);
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
        std::ofstream script(cli_runtime, std::ios::trunc);
        script << "#!/bin/sh\n"
               << "sleep 10\n";
    }
    chmod(cli_runtime.c_str(), 0755);
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
