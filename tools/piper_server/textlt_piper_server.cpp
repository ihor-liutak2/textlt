#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "civetweb.h"
#include "nlohmann/json.hpp"

using Json = nlohmann::ordered_json;

namespace {

struct ServerConfig {
    std::string host = "127.0.0.1";
    std::string port = "59123";
    std::string model_path;
    std::string config_path;
    std::string models_dir;
    bool use_cuda = false;
};

struct ServerState {
    explicit ServerState(ServerConfig config_value)
        : config(std::move(config_value)),
          started_at(std::chrono::steady_clock::now()) {}

    ServerConfig config;
    const std::chrono::steady_clock::time_point started_at;
    std::atomic_bool shutdown_requested{false};
    std::atomic<unsigned long long> request_count{0};
};

std::string JsonResponse(const Json& payload) {
    return payload.dump(2);
}

void SendJson(struct mg_connection* conn, int status, const Json& payload) {
    const std::string body = JsonResponse(payload);
    const char* status_text = status == 200 ? "OK" :
        status == 400 ? "Bad Request" :
        status == 404 ? "Not Found" :
        status == 501 ? "Not Implemented" : "Error";
    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %zu\r\n"
              "Connection: close\r\n\r\n",
              status,
              status_text,
              body.size());
    mg_write(conn, body.data(), body.size());
}

std::string ReadRequestBody(struct mg_connection* conn) {
    const struct mg_request_info* info = mg_get_request_info(conn);
    if (!info || info->content_length <= 0) {
        return {};
    }
    std::string body;
    body.resize(static_cast<size_t>(info->content_length));
    size_t offset = 0;
    while (offset < body.size()) {
        const int read = mg_read(
            conn,
            &body[offset],
            static_cast<size_t>(body.size() - offset));
        if (read <= 0) {
            break;
        }
        offset += static_cast<size_t>(read);
    }
    body.resize(offset);
    return body;
}

bool IsMethod(struct mg_connection* conn, const char* method) {
    const struct mg_request_info* info = mg_get_request_info(conn);
    return info && info->request_method && std::strcmp(info->request_method, method) == 0;
}

int CurrentProcessId() {
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

long long UptimeSeconds(const ServerState& state) {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - state.started_at).count();
}

int HealthHandler(struct mg_connection* conn, void* data) {
    auto* state = static_cast<ServerState*>(data);
    state->request_count.fetch_add(1);
    SendJson(conn, 200, {
        {"ok", true},
        {"service", "textlt-piper-server"},
        {"backend", "not-linked"},
        {"model_path", state->config.model_path},
        {"config_path", state->config.config_path},
        {"models_dir", state->config.models_dir},
        {"use_cuda", state->config.use_cuda},
        {"pid", CurrentProcessId()},
        {"uptime_seconds", UptimeSeconds(*state)},
        {"requests", state->request_count.load()}
    });
    return 200;
}

int StatusHandler(struct mg_connection* conn, void* data) {
    auto* state = static_cast<ServerState*>(data);
    state->request_count.fetch_add(1);
    SendJson(conn, 200, {
        {"running", true},
        {"backend_ready", false},
        {"backend_message", "Piper/ONNX persistent backend is not linked yet"},
        {"host", state->config.host},
        {"port", state->config.port},
        {"model_path", state->config.model_path},
        {"config_path", state->config.config_path},
        {"models_dir", state->config.models_dir},
        {"use_cuda", state->config.use_cuda},
        {"pid", CurrentProcessId()},
        {"uptime_seconds", UptimeSeconds(*state)},
        {"requests", state->request_count.load()}
    });
    return 200;
}

int ShutdownHandler(struct mg_connection* conn, void* data) {
    auto* state = static_cast<ServerState*>(data);
    state->request_count.fetch_add(1);
    if (!IsMethod(conn, "POST")) {
        SendJson(conn, 400, {{"ok", false}, {"error", "POST required"}});
        return 400;
    }
    state->shutdown_requested.store(true);
    SendJson(conn, 200, {{"ok", true}, {"message", "shutdown requested"}});
    return 200;
}

int SynthesizeHandler(struct mg_connection* conn, void* data) {
    auto* state = static_cast<ServerState*>(data);
    state->request_count.fetch_add(1);
    if (!IsMethod(conn, "POST")) {
        SendJson(conn, 400, {{"ok", false}, {"error", "POST required"}});
        return 400;
    }

    const std::string body = ReadRequestBody(conn);
    Json request = Json::parse(body, nullptr, false);
    if (request.is_discarded() || !request.is_object()) {
        SendJson(conn, 400, {{"ok", false}, {"error", "Invalid JSON request"}});
        return 400;
    }

    const std::string text = request.value("text", "");
    const std::string output_wav_path = request.value("output_wav_path", "");
    const double noise_scale = request.value("noise_scale", 0.667);
    const double sentence_silence_seconds = request.value("sentence_silence_seconds", 0.15);
    const int speaker_id = request.value("speaker_id", 0);

    SendJson(conn, 501, {
        {"ok", false},
        {"error", "Persistent Piper backend is not linked yet"},
        {"accepted_options", {
            {"text_length", text.size()},
            {"output_wav_path", output_wav_path},
            {"noise_scale", noise_scale},
            {"sentence_silence_seconds", sentence_silence_seconds},
            {"speaker_id", speaker_id},
            {"use_cuda", state->config.use_cuda}
        }}
    });
    return 501;
}

std::string NextArg(int& index, int argc, char** argv) {
    if (index + 1 >= argc) {
        return {};
    }
    ++index;
    return argv[index] ? argv[index] : "";
}

ServerConfig ParseArgs(int argc, char** argv) {
    ServerConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--host") {
            config.host = NextArg(i, argc, argv);
        } else if (arg == "--port") {
            config.port = NextArg(i, argc, argv);
        } else if (arg == "--model") {
            config.model_path = NextArg(i, argc, argv);
        } else if (arg == "--config") {
            config.config_path = NextArg(i, argc, argv);
        } else if (arg == "--models-dir") {
            config.models_dir = NextArg(i, argc, argv);
        } else if (arg == "--cuda") {
            config.use_cuda = true;
        }
    }
    if (config.host.empty()) {
        config.host = "127.0.0.1";
    }
    if (config.port.empty()) {
        config.port = "59123";
    }
    return config;
}

} // namespace

int main(int argc, char** argv) {
    ServerConfig config = ParseArgs(argc, argv);
    ServerState state(config);

    if (!mg_init_library(MG_FEATURES_DEFAULT)) {
        std::cerr << "Failed to initialize CivetWeb\n";
        return 1;
    }

    const std::string listen = config.host + ":" + config.port;
    const char* options[] = {
        "listening_ports", listen.c_str(),
        "num_threads", "2",
        "enable_keep_alive", "no",
        "request_timeout_ms", "30000",
        nullptr
    };

    struct mg_callbacks callbacks{};
    struct mg_context* context = mg_start(&callbacks, &state, options);
    if (!context) {
        std::cerr << "Failed to start textlt-piper-server on " << listen << "\n";
        mg_exit_library();
        return 2;
    }

    mg_set_request_handler(context, "/health", HealthHandler, &state);
    mg_set_request_handler(context, "/status", StatusHandler, &state);
    mg_set_request_handler(context, "/shutdown", ShutdownHandler, &state);
    mg_set_request_handler(context, "/synthesize", SynthesizeHandler, &state);

    std::cout << "textlt-piper-server listening on " << listen << "\n";
    while (!state.shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    mg_stop(context);
    mg_exit_library();
    return 0;
}
