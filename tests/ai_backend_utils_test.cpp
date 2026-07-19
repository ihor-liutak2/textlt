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
#include "ai/ai_model_catalog.hpp"
#include "ai/local_llama_server.hpp"
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

    const auto& gpu_models = textlt::BuiltInGpuModels();
    assert(gpu_models.size() == 2);
    assert(gpu_models[0].filename == "gemma-3-4b-it-q4_0.gguf");
    assert(gpu_models[0].tier == "Standard");
    assert(gpu_models[0].recommended_vram_mb == 4096);
    assert(gpu_models[0].download_url.find("15f73f5eee9c28f53afefef5723e29680c2fc78a") !=
           std::string::npos);
    assert(gpu_models[1].filename == "gemma-4-E2B_q4_0-it.gguf");
    assert(gpu_models[1].tier == "Advanced");
    assert(gpu_models[1].recommended_vram_mb == 6144);
    assert(gpu_models[1].download_url.find("347eef722ec7f151f37d1ef0b5c7c77d8de4efcb") !=
           std::string::npos);
    assert(textlt::FindBuiltInAiModel(gpu_models[0].filename) == &gpu_models[0]);
    assert(textlt::FindBuiltInAiModel("missing.gguf") == nullptr);

    textlt::AiPromptRequest request;
    request.text = "text";
    const textlt::AiBackend invalid_local({
        "", AiProvider::LocalLlamaCpp, "local:../model.gguf", 5});
    const textlt::AiBackendResult invalid_result = invalid_local.Run(request);
    assert(!invalid_result.success);
    assert(invalid_result.error.find("filename is invalid") != std::string::npos);

#ifndef _WIN32
    assert(AiBackend::RecommendedMaxOutputTokens(request) >= 64);
    assert(AiBackend::RecommendedMaxOutputTokens(request) < 512);
    textlt::AiPromptRequest long_request = request;
    long_request.text = std::string(1200, 'a');
    assert(AiBackend::RecommendedMaxOutputTokens(long_request) >
           AiBackend::RecommendedMaxOutputTokens(request));

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
    const std::filesystem::path server_runtime = runtime_dir / "llama-server";
    const std::filesystem::path model = models_dir / "test.gguf";
    const std::filesystem::path starts_file = test_root / "starts.txt";
    std::ofstream(model).put('\n');
    {
        std::ofstream script(server_runtime);
        script << R"PY(#!/usr/bin/env python3
import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

port = 11435
alias = "test.gguf"
args = sys.argv[1:]
if "--list-devices" in args:
    probe_delay_ms = int(os.environ.get("TEXTLT_TEST_GPU_PROBE_DELAY_MS", "0"))
    if probe_delay_ms > 0:
        time.sleep(probe_delay_ms / 1000.0)
    print("Available devices:")
    if os.environ.get("TEXTLT_TEST_NO_GPU", "0") != "1":
        print("  CUDA0: TextLT Test GPU (8192 MiB, 7000 MiB free)")
    sys.exit(0)
assert "--no-mmproj" in args
assert "--n-gpu-layers" not in args
for i, arg in enumerate(args):
    if arg == "--port" and i + 1 < len(args):
        port = int(args[i + 1])
    if arg == "--alias" and i + 1 < len(args):
        alias = args[i + 1]
with open(os.environ["TEXTLT_TEST_SERVER_STARTS"], "a", encoding="utf-8") as f:
    f.write("start\n")
startup_delay_ms = int(os.environ.get("TEXTLT_TEST_SERVER_HEALTH_DELAY_MS", "0"))
if startup_delay_ms > 0:
    time.sleep(startup_delay_ms / 1000.0)

class Handler(BaseHTTPRequestHandler):
    active_requests = 0
    def log_message(self, fmt, *args):
        pass
    def send_json(self, status, value):
        body = json.dumps(value).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_GET(self):
        if self.path == "/health":
            self.send_json(200, {"status": "ok"})
        elif self.path == "/v1/models":
            self.send_json(200, {"data": [{"id": alias}]})
        elif self.path == "/slots":
            self.send_json(200, [{"id": 0, "is_processing": Handler.active_requests > 0}])
        else:
            self.send_json(404, {"error": "not found"})
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length) or b"{}")
        messages = body.get("messages", [])
        assert len(messages) == 1
        assert messages[0].get("role") == "user"
        assert 64 <= int(body.get("max_tokens", 0)) < 512
        assert "[end of text]" in body.get("stop", [])
        assert "chat_template_kwargs" not in body
        text = " ".join(str(m.get("content", "")) for m in messages)
        Handler.active_requests += 1
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        chunks = ["Corrected ", "text. [end of text]"]
        if "slow" in text:
            chunks = ["First ", "response."]
        try:
            for index, chunk in enumerate(chunks):
                event = {"choices": [{"delta": {"content": chunk}, "finish_reason": None}]}
                self.wfile.write(("data: " + json.dumps(event) + "\n\n").encode())
                self.wfile.flush()
                if "slow" in text and index == 0:
                    for _ in range(200):
                        time.sleep(0.05)
                        self.wfile.write(b": keepalive\n\n")
                        self.wfile.flush()
            final = {
                "choices": [{"delta": {}, "finish_reason": "stop", "stop_type": "word"}],
                "usage": {"prompt_tokens": 12, "completion_tokens": 5},
                "timings": {"prompt_ms": 5.0, "predicted_ms": 25.0,
                            "predicted_n": 5, "predicted_per_second": 200.0}
            }
            self.wfile.write(("data: " + json.dumps(final) + "\n\n").encode())
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            Handler.active_requests -= 1

ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
)PY";
    }
    chmod(server_runtime.c_str(), 0755);
    const int port = 19000 + static_cast<int>(test_id % 10000);
    const std::string port_text = std::to_string(port);
    setenv("XDG_DATA_HOME", test_root.c_str(), 1);
    setenv("TEXTLT_LLAMA_SERVER_PORT", port_text.c_str(), 1);
    setenv("TEXTLT_TEST_SERVER_STARTS", starts_file.c_str(), 1);

    textlt::AiBackendSettings local_settings;
    local_settings.provider = AiProvider::LocalLlamaCpp;
    local_settings.selected_model_key = "local:test.gguf";
    local_settings.timeout_seconds = 5;
    local_settings.max_output_tokens = 0;
    local_settings.local_threads = 3;

    const textlt::AiConnectionResult ready_local =
        textlt::AiBackend(local_settings).CheckSelectedModelReady();
    assert(ready_local.success);

    const textlt::AiBackendResult first = textlt::AiBackend(local_settings).Run(request);
    assert(first.success);
    assert(first.text == "Corrected text.");
    assert(first.finish_reason == textlt::AiFinishReason::StopMarker);
    assert(first.generated_tokens == 5);
    assert(first.tokens_per_second == 200.0);
    assert(first.model_load_ms > 0.0);
    assert(first.time_to_first_token_ms > 0.0);

    const textlt::AiBackendResult second = textlt::AiBackend(local_settings).Run(request);
    assert(second.success);
    assert(second.model_load_ms == 0.0);
    std::ifstream starts_input(starts_file);
    const std::string starts((std::istreambuf_iterator<char>(starts_input)),
                             std::istreambuf_iterator<char>());
    assert(starts == "start\n");

    textlt::AiPromptRequest slow_request = request;
    slow_request.text = "slow";
    textlt::RemoteCommandControl task_control;
    std::atomic<bool> saw_output{false};
    textlt::AiBackendResult cancelled;
    std::thread request_thread([&] {
        cancelled = textlt::AiBackend(local_settings).Run(
            slow_request,
            nullptr,
            [&](const std::string& generated) {
                if (generated.find("First") != std::string::npos) {
                    saw_output.store(true);
                }
            },
            &task_control);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!saw_output.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(saw_output.load());
    task_control.RequestStop();
    request_thread.join();
    assert(!cancelled.success);
    assert(cancelled.finish_reason == textlt::AiFinishReason::Cancelled);
    assert(textlt::LocalLlamaServerManager::Instance().IsReadyFor("test.gguf"));

    const textlt::AiBackendResult after_cancel =
        textlt::AiBackend(local_settings).Run(request);
    assert(after_cancel.success);

    textlt::LocalLlamaServerManager::Instance().Unload();
    assert(!textlt::LocalLlamaServerManager::Instance().IsReadyFor("test.gguf"));

    const std::filesystem::path gpu_model =
        models_dir / "gemma-3-4b-it-q4_0.gguf";
    std::ofstream(gpu_model).put('\n');
    textlt::AiBackendSettings gpu_settings = local_settings;
    gpu_settings.selected_model_key = "local:" + gpu_model.filename().string();
    assert(textlt::AiBackend(gpu_settings).CheckSelectedModelReady().success);
    std::string gpu_start_error;
    assert(textlt::LocalLlamaServerManager::Instance().EnsureRunning(
        gpu_model.filename().string(), 2, 5, gpu_start_error));
    const textlt::LocalLlamaServerSnapshot gpu_snapshot =
        textlt::LocalLlamaServerManager::Instance().Snapshot();
    assert(gpu_snapshot.gpu_device.find("CUDA0") != std::string::npos);
    assert(gpu_snapshot.gpu_total_memory_mb == 8192);
    assert(gpu_snapshot.gpu_free_memory_mb == 7000);
    textlt::LocalLlamaServerManager::Instance().Unload();

    setenv("TEXTLT_TEST_NO_GPU", "1", 1);
    const textlt::AiConnectionResult no_gpu_ready =
        textlt::AiBackend(gpu_settings).CheckSelectedModelReady();
    assert(!no_gpu_ready.success);
    assert(no_gpu_ready.error.find("no compatible GPU") != std::string::npos);
    std::string no_gpu_error;
    assert(!textlt::LocalLlamaServerManager::Instance().EnsureRunning(
        gpu_model.filename().string(), 2, 5, no_gpu_error));
    assert(no_gpu_error.find("no compatible GPU") != std::string::npos);
    unsetenv("TEXTLT_TEST_NO_GPU");

    setenv("TEXTLT_TEST_GPU_PROBE_DELAY_MS", "10000", 1);
    textlt::RemoteCommandControl gpu_probe_control;
    textlt::AiConnectionResult cancelled_gpu_probe;
    std::thread gpu_probe_thread([&] {
        cancelled_gpu_probe =
            textlt::AiBackend(gpu_settings).CheckSelectedModelReady(
                nullptr, &gpu_probe_control);
    });
    const auto gpu_probe_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!gpu_probe_control.IsRunning() &&
           std::chrono::steady_clock::now() < gpu_probe_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(gpu_probe_control.IsRunning());
    const auto gpu_probe_stop_started = std::chrono::steady_clock::now();
    gpu_probe_control.RequestStop();
    gpu_probe_thread.join();
    assert(!cancelled_gpu_probe.success);
    assert(cancelled_gpu_probe.error.find("stopped") != std::string::npos);
    assert(std::chrono::steady_clock::now() - gpu_probe_stop_started <
           std::chrono::seconds(3));
    unsetenv("TEXTLT_TEST_GPU_PROBE_DELAY_MS");

    const std::filesystem::path slow_start_model = models_dir / "slow-start.gguf";
    std::ofstream(slow_start_model).put('\n');
    setenv("TEXTLT_TEST_SERVER_HEALTH_DELAY_MS", "10000", 1);
    bool slow_start_ready = true;
    std::string slow_start_error;
    std::thread slow_start_thread([&] {
        slow_start_ready = textlt::LocalLlamaServerManager::Instance().EnsureRunning(
            "slow-start.gguf", 2, 30, slow_start_error);
    });
    const auto starting_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (textlt::LocalLlamaServerManager::Instance().Snapshot().state !=
               textlt::LocalLlamaServerState::Starting &&
           std::chrono::steady_clock::now() < starting_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(textlt::LocalLlamaServerManager::Instance().Snapshot().state ==
           textlt::LocalLlamaServerState::Starting);
    const auto unload_started = std::chrono::steady_clock::now();
    textlt::LocalLlamaServerManager::Instance().Unload();
    slow_start_thread.join();
    assert(!slow_start_ready);
    assert(!slow_start_error.empty());
    assert(std::chrono::steady_clock::now() - unload_started < std::chrono::seconds(3));
    assert(textlt::LocalLlamaServerManager::Instance().Snapshot().state ==
           textlt::LocalLlamaServerState::Stopped);

    std::atomic<bool> startup_cancel{false};
    bool cancelled_start_ready = true;
    std::string cancelled_start_error;
    std::thread cancelled_start_thread([&] {
        cancelled_start_ready = textlt::LocalLlamaServerManager::Instance().EnsureRunning(
            "slow-start.gguf", 2, 30, cancelled_start_error, &startup_cancel);
    });
    const auto cancelled_start_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (textlt::LocalLlamaServerManager::Instance().Snapshot().state !=
               textlt::LocalLlamaServerState::Starting &&
           std::chrono::steady_clock::now() < cancelled_start_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(textlt::LocalLlamaServerManager::Instance().Snapshot().state ==
           textlt::LocalLlamaServerState::Starting);
    const auto cancel_start_at = std::chrono::steady_clock::now();
    startup_cancel.store(true);
    cancelled_start_thread.join();
    assert(!cancelled_start_ready);
    assert(cancelled_start_error.find("cancelled") != std::string::npos);
    assert(std::chrono::steady_clock::now() - cancel_start_at <
           std::chrono::seconds(3));
    assert(textlt::LocalLlamaServerManager::Instance().Snapshot().state ==
           textlt::LocalLlamaServerState::Stopped);
    unsetenv("TEXTLT_TEST_SERVER_HEALTH_DELAY_MS");

    std::filesystem::remove_all(test_root);
#endif
    return 0;
}
