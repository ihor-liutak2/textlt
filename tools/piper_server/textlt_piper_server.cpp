#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <ctime>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "civetweb.h"
#include "nlohmann/json.hpp"

using Json = nlohmann::ordered_json;

namespace {

struct ServerConfig {
    std::string host = "127.0.0.1";
    std::string port = "59123";
    std::string piper_path;
    std::string model_path;
    std::string config_path;
    std::string models_dir;
    std::string log_dir;
    bool use_cuda = false;
};

struct BackendKey {
    std::string model_path;
    std::string config_path;
    bool use_cuda = false;
    double noise_scale = 0.667;
    double sentence_silence_seconds = 0.15;
    int speaker_id = 0;

    bool operator==(const BackendKey& other) const {
        return model_path == other.model_path &&
            config_path == other.config_path &&
            use_cuda == other.use_cuda &&
            noise_scale == other.noise_scale &&
            sentence_silence_seconds == other.sentence_silence_seconds &&
            speaker_id == other.speaker_id;
    }
};

std::string JsonResponse(const Json& payload) {
    return payload.dump(2);
}

void SendJson(struct mg_connection* conn, int status, const Json& payload) {
    const std::string body = JsonResponse(payload);
    const char* status_text = status == 200 ? "OK" :
        status == 400 ? "Bad Request" :
        status == 404 ? "Not Found" :
        status == 500 ? "Internal Server Error" :
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

std::optional<std::string> GetEnvValue(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
        return std::nullopt;
    }
    std::string value(buffer);
    free(buffer);
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
#else
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

std::filesystem::path DefaultLogDirectory() {
#ifdef _WIN32
    const std::optional<std::string> local_app_data = GetEnvValue("LOCALAPPDATA");
    if (local_app_data) {
        return std::filesystem::path(*local_app_data) / "textlt" / "piper" / "logs";
    }
    const std::optional<std::string> home = GetEnvValue("USERPROFILE");
    return home ? std::filesystem::path(*home) / "AppData" / "Local" / "textlt" / "piper" / "logs"
                : std::filesystem::path(".") / "logs";
#else
    const std::optional<std::string> data_home = GetEnvValue("XDG_DATA_HOME");
    if (data_home) {
        return std::filesystem::path(*data_home) / "textlt" / "piper" / "logs";
    }
    const std::optional<std::string> home = GetEnvValue("HOME");
    return home ? std::filesystem::path(*home) / ".local" / "share" / "textlt" / "piper" / "logs"
                : std::filesystem::path(".") / "logs";
#endif
}

std::string NowStamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return buffer;
}

std::string VoiceLabelFromModelPath(const std::string& model_path) {
    const std::filesystem::path model(model_path);
    const std::string label = model.stem().string();
    return label.empty() ? "-" : label;
}

std::string NormalizeTextLine(std::string text) {
    for (char& ch : text) {
        if (ch == '\r' || ch == '\n') {
            ch = ' ';
        }
    }
    return text;
}

std::string FormatCommand(const std::vector<std::string>& args) {
    std::ostringstream out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ' ';
        }
        out << '"' << args[i] << '"';
    }
    return out.str();
}

class ServerState {
public:
    explicit ServerState(ServerConfig config_value)
        : config_(std::move(config_value)),
          started_at_(std::chrono::steady_clock::now()) {
        if (config_.log_dir.empty()) {
            config_.log_dir = DefaultLogDirectory().string();
        }
        std::error_code error;
        std::filesystem::create_directories(config_.log_dir, error);
        output_dir_ = std::filesystem::temp_directory_path(error) /
            ("textlt-piper-server-" + std::to_string(CurrentProcessId()));
        if (!error) {
            std::filesystem::create_directories(output_dir_, error);
        }
    }

    ~ServerState() {
        std::lock_guard<std::mutex> lock(mutex_);
        StopChildLocked();
    }

    long long UptimeSeconds() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started_at_).count();
    }

    void Log(const std::string& message) const {
        const std::filesystem::path path = ServerLogPath();
        std::ofstream log(path, std::ios::app | std::ios::binary);
        if (log) {
            log << '[' << NowStamp() << "] " << message << '\n';
        }
        std::cout << "[textlt-piper-server] " << message << '\n';
    }

    Json StatusJson() {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool voice_ready = child_running_ && voice_ready_;
        return {
            {"running", true},
            {"backend_ready", voice_ready},
            {"voice_ready", voice_ready},
            {"voice_status", VoiceStatusLocked()},
            {"backend_message", backend_message_},
            {"last_error", last_error_},
            {"active_voice", active_key_ ? VoiceLabelFromModelPath(active_key_->model_path) : "-"},
            {"active_model_path", active_key_ ? active_key_->model_path : ""},
            {"host", config_.host},
            {"port", config_.port},
            {"piper_path", config_.piper_path},
            {"piper_log_path", PiperLogPath().string()},
            {"server_log_path", ServerLogPath().string()},
            {"model_path", active_key_ ? active_key_->model_path : config_.model_path},
            {"config_path", active_key_ ? active_key_->config_path : config_.config_path},
            {"models_dir", config_.models_dir},
            {"use_cuda", active_key_ ? active_key_->use_cuda : config_.use_cuda},
            {"child_pid", child_pid_},
            {"pid", CurrentProcessId()},
            {"uptime_seconds", UptimeSeconds()},
            {"requests", request_count_.load()}
        };
    }

    Json HealthJson() {
        Json status = StatusJson();
        status["ok"] = true;
        status["service"] = "textlt-piper-server";
        return status;
    }

    bool PreloadInitialVoice() {
        BackendKey key;
        key.model_path = config_.model_path;
        key.config_path = config_.config_path;
        key.use_cuda = config_.use_cuda;
        if (key.model_path.empty() || key.config_path.empty()) {
            Log("preload: skipped because model/config is empty");
            return true;
        }
        std::string error;
        const bool ok = EnsureChild(key, &error);
        if (!ok) {
            Log("preload: failed: " + error);
        } else {
            Log("preload: voice is ready");
        }
        return ok;
    }

    bool Synthesize(const Json& request, std::string* error) {
        const std::string text = request.value("text", "");
        const std::string output_wav_path = request.value("output_wav_path", "");
        if (text.empty()) {
            if (error) {
                *error = "Text is empty";
            }
            return false;
        }
        if (output_wav_path.empty()) {
            if (error) {
                *error = "output_wav_path is empty";
            }
            return false;
        }

        BackendKey key;
        key.model_path = request.value("model_path", config_.model_path);
        key.config_path = request.value("config_path", config_.config_path);
        key.use_cuda = request.value("use_cuda", config_.use_cuda);
        key.noise_scale = request.value("noise_scale", 0.667);
        key.sentence_silence_seconds = request.value("sentence_silence_seconds", 0.15);
        key.speaker_id = request.value("speaker_id", 0);
        if (key.model_path.empty() || key.config_path.empty()) {
            if (error) {
                *error = "Piper model/config path is empty";
            }
            return false;
        }

        std::lock_guard<std::mutex> request_lock(synthesis_mutex_);
        if (!EnsureChild(key, error)) {
            return false;
        }

        ClearOutputWavFiles();
        const std::set<std::string> before = ExistingWavFiles();
        const std::string line = NormalizeTextLine(text) + "\n";
        if (!WriteToChild(line, error)) {
            return false;
        }

        std::filesystem::path generated;
        if (!WaitForGeneratedWav(before, &generated, error)) {
            return false;
        }

        std::error_code fs_error;
        const std::filesystem::path output_path(output_wav_path);
        std::filesystem::create_directories(output_path.parent_path(), fs_error);
        fs_error.clear();
        std::filesystem::copy_file(
            generated,
            output_path,
            std::filesystem::copy_options::overwrite_existing,
            fs_error);
        if (fs_error) {
            if (error) {
                *error = "Cannot copy generated WAV: " + fs_error.message();
            }
            return false;
        }
        return true;
    }

    ServerConfig config_;
    const std::chrono::steady_clock::time_point started_at_;
    std::atomic_bool shutdown_requested{false};
    std::atomic<unsigned long long> request_count_{0};

private:
    std::filesystem::path ServerLogPath() const {
        return std::filesystem::path(config_.log_dir) / "textlt-piper-server.log";
    }

    std::filesystem::path PiperLogPath() const {
        return std::filesystem::path(config_.log_dir) / "textlt-piper-child.log";
    }

    std::string VoiceStatusLocked() const {
        if (voice_ready_) {
            return "ready";
        }
        if (loading_) {
            return "loading";
        }
        if (!last_error_.empty()) {
            return "failed";
        }
        return "not_loaded";
    }

    std::vector<std::string> BuildPiperArgs(const BackendKey& key) const {
        std::vector<std::string> args = {
            config_.piper_path,
            "--model", key.model_path,
            "--config", key.config_path,
            "--output_dir", output_dir_.string(),
            "--noise_scale", std::to_string(key.noise_scale),
            "--sentence_silence", std::to_string(key.sentence_silence_seconds)
        };
        if (key.use_cuda) {
            args.push_back("--use-cuda");
        }
        if (key.speaker_id > 0) {
            args.push_back("--speaker");
            args.push_back(std::to_string(key.speaker_id));
        }
        return args;
    }

    bool EnsureChild(const BackendKey& key, std::string* error) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (child_running_ && voice_ready_ && active_key_ && *active_key_ == key && IsChildAliveLocked()) {
            backend_message_ = "Piper voice already loaded";
            return true;
        }

        StopChildLocked();
        if (!StartChildLocked(key, error)) {
            return false;
        }

        const bool ready = ready_cv_.wait_for(
            lock,
            std::chrono::seconds(30),
            [&] { return voice_ready_ || !last_error_.empty(); });
        if (!ready || !voice_ready_) {
            if (last_error_.empty()) {
                last_error_ = "Timed out waiting for Piper voice load message";
            }
            backend_message_ = last_error_;
            if (error) {
                *error = last_error_;
            }
            StopChildLocked();
            return false;
        }
        backend_message_ = "Piper voice loaded";
        return true;
    }

    bool StartChildLocked(const BackendKey& key, std::string* error) {
        if (config_.piper_path.empty()) {
            last_error_ = "Piper executable path is empty";
            if (error) {
                *error = last_error_;
            }
            return false;
        }
        std::error_code fs_error;
        std::filesystem::create_directories(output_dir_, fs_error);
        std::filesystem::create_directories(config_.log_dir, fs_error);

        active_key_ = key;
        stopping_child_.store(false);
        voice_ready_ = false;
        loading_ = true;
        last_error_.clear();
        backend_message_ = "Starting Piper process";

        const std::vector<std::string> args = BuildPiperArgs(key);
        AppendChildLogSeparator(args, key);
        Log("backend: command " + FormatCommand(args));

#ifdef _WIN32
        last_error_ = "Persistent Piper child process is not implemented on Windows yet";
        loading_ = false;
        backend_message_ = last_error_;
        if (error) {
            *error = last_error_;
        }
        return false;
#else
        int stdin_pipe[2] = {-1, -1};
        int log_pipe[2] = {-1, -1};
        if (pipe(stdin_pipe) != 0 || pipe(log_pipe) != 0) {
            last_error_ = std::string("pipe failed: ") + std::strerror(errno);
            CloseFd(stdin_pipe[0]);
            CloseFd(stdin_pipe[1]);
            CloseFd(log_pipe[0]);
            CloseFd(log_pipe[1]);
            loading_ = false;
            if (error) {
                *error = last_error_;
            }
            return false;
        }

        const pid_t pid = fork();
        if (pid < 0) {
            last_error_ = std::string("fork failed: ") + std::strerror(errno);
            CloseFd(stdin_pipe[0]);
            CloseFd(stdin_pipe[1]);
            CloseFd(log_pipe[0]);
            CloseFd(log_pipe[1]);
            loading_ = false;
            if (error) {
                *error = last_error_;
            }
            return false;
        }

        if (pid == 0) {
            dup2(stdin_pipe[0], STDIN_FILENO);
            dup2(log_pipe[1], STDOUT_FILENO);
            dup2(log_pipe[1], STDERR_FILENO);
            CloseFd(stdin_pipe[0]);
            CloseFd(stdin_pipe[1]);
            CloseFd(log_pipe[0]);
            CloseFd(log_pipe[1]);

            std::vector<char*> argv;
            argv.reserve(args.size() + 1);
            for (const std::string& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);
            execv(config_.piper_path.c_str(), argv.data());
            const std::string message = std::string("execv failed for ") + config_.piper_path + ": " + std::strerror(errno) + "\n";
            write(STDERR_FILENO, message.data(), message.size());
            _exit(127);
        }

        CloseFd(stdin_pipe[0]);
        CloseFd(log_pipe[1]);
        child_pid_ = static_cast<int>(pid);
        child_stdin_fd_ = stdin_pipe[1];
        child_log_fd_ = log_pipe[0];
        child_running_ = true;
        reader_thread_ = std::thread([this] { ReadChildLogLoop(); });
        return true;
#endif
    }

    void AppendChildLogSeparator(const std::vector<std::string>& args, const BackendKey& key) const {
        std::ofstream log(PiperLogPath(), std::ios::app | std::ios::binary);
        if (!log) {
            return;
        }
        log << "\n----- Piper child start " << NowStamp() << " -----\n";
        log << "command: " << FormatCommand(args) << '\n';
        log << "model: " << key.model_path << '\n';
        log << "config: " << key.config_path << '\n';
        log << "cuda: " << (key.use_cuda ? "true" : "false") << '\n';
    }

    void ReadChildLogLoop() {
#ifndef _WIN32
        std::ofstream log(PiperLogPath(), std::ios::app | std::ios::binary);
        std::string line;
        char buffer[512];
        while (true) {
            const ssize_t count = read(child_log_fd_, buffer, sizeof(buffer));
            if (count <= 0) {
                break;
            }
            if (log) {
                log.write(buffer, count);
                log.flush();
            }
            for (ssize_t i = 0; i < count; ++i) {
                const char ch = buffer[i];
                if (ch == '\n') {
                    HandleChildLogLine(line);
                    line.clear();
                } else if (ch != '\r') {
                    line.push_back(ch);
                }
            }
        }
        if (!line.empty()) {
            HandleChildLogLine(line);
        }
#endif
    }

    void HandleChildLogLine(const std::string& line) {
        if (stopping_child_.load()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (line.find("Loaded voice in") != std::string::npos) {
            voice_ready_ = true;
            loading_ = false;
            backend_message_ = "Piper voice loaded";
            ready_cv_.notify_all();
        }
        if (line.find("execv failed") != std::string::npos ||
            line.find("error") != std::string::npos ||
            line.find("Error") != std::string::npos) {
            if (!voice_ready_) {
                last_error_ = line;
                backend_message_ = line;
                ready_cv_.notify_all();
            }
        }
    }

    bool WriteToChild(const std::string& text, std::string* error) {
#ifdef _WIN32
        if (error) {
            *error = "Persistent Piper child process is not implemented on Windows yet";
        }
        return false;
#else
        std::lock_guard<std::mutex> lock(mutex_);
        if (!child_running_ || child_stdin_fd_ < 0 || !IsChildAliveLocked()) {
            if (error) {
                *error = "Piper process is not running";
            }
            return false;
        }
        size_t written = 0;
        while (written < text.size()) {
            const ssize_t count = write(child_stdin_fd_, text.data() + written, text.size() - written);
            if (count <= 0) {
                if (error) {
                    *error = std::string("Cannot write to Piper stdin: ") + std::strerror(errno);
                }
                return false;
            }
            written += static_cast<size_t>(count);
        }
        return true;
#endif
    }


    void ClearOutputWavFiles() const {
        std::error_code error;
        if (!std::filesystem::exists(output_dir_, error)) {
            return;
        }
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(output_dir_, error)) {
            if (!error && entry.path().extension() == ".wav") {
                std::filesystem::remove(entry.path(), error);
            }
            error.clear();
        }
    }

    std::set<std::string> ExistingWavFiles() const {
        std::set<std::string> files;
        std::error_code error;
        if (!std::filesystem::exists(output_dir_, error)) {
            return files;
        }
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(output_dir_, error)) {
            if (!error && entry.is_regular_file(error) && entry.path().extension() == ".wav") {
                files.insert(entry.path().string());
            }
            error.clear();
        }
        return files;
    }

    bool WaitForGeneratedWav(const std::set<std::string>& before,
                             std::filesystem::path* generated,
                             std::string* error) const {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline) {
            std::error_code fs_error;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(output_dir_, fs_error)) {
                if (fs_error) {
                    break;
                }
                const std::filesystem::path path = entry.path();
                if (path.extension() != ".wav" || before.count(path.string()) != 0) {
                    continue;
                }
                const uintmax_t size1 = std::filesystem::file_size(path, fs_error);
                if (fs_error || size1 <= 44) {
                    fs_error.clear();
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
                const uintmax_t size2 = std::filesystem::file_size(path, fs_error);
                if (!fs_error && size1 == size2 && size2 > 44) {
                    *generated = path;
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (error) {
            *error = "Timed out waiting for Piper WAV output";
        }
        return false;
    }

    bool IsChildAliveLocked() const {
#ifdef _WIN32
        return child_running_;
#else
        return child_pid_ > 0 && kill(static_cast<pid_t>(child_pid_), 0) == 0;
#endif
    }

    void StopChildLocked() {
        stopping_child_.store(true);
#ifndef _WIN32
        if (child_stdin_fd_ >= 0) {
            CloseFd(child_stdin_fd_);
            child_stdin_fd_ = -1;
        }
        if (child_pid_ > 0) {
            kill(static_cast<pid_t>(child_pid_), SIGTERM);
            int status = 0;
            waitpid(static_cast<pid_t>(child_pid_), &status, 0);
        }
        child_pid_ = -1;
        child_running_ = false;
        voice_ready_ = false;
        loading_ = false;
        if (reader_thread_.joinable()) {
            reader_thread_.join();
        }
        if (child_log_fd_ >= 0) {
            CloseFd(child_log_fd_);
            child_log_fd_ = -1;
        }
#else
        child_running_ = false;
        voice_ready_ = false;
        loading_ = false;
#endif
    }

#ifndef _WIN32
    static void CloseFd(int fd) {
        if (fd >= 0) {
            close(fd);
        }
    }
#endif

    mutable std::mutex mutex_;
    std::mutex synthesis_mutex_;
    std::condition_variable ready_cv_;
    std::filesystem::path output_dir_;
    std::optional<BackendKey> active_key_;
    std::string backend_message_ = "Piper voice is not loaded";
    std::string last_error_;
    bool voice_ready_ = false;
    bool loading_ = false;
    bool child_running_ = false;
    std::atomic_bool stopping_child_{false};
    int child_pid_ = -1;
    int child_stdin_fd_ = -1;
    int child_log_fd_ = -1;
    std::thread reader_thread_;
};

int HealthHandler(struct mg_connection* conn, void* data) {
    auto* state = static_cast<ServerState*>(data);
    state->request_count_.fetch_add(1);
    SendJson(conn, 200, state->HealthJson());
    return 200;
}

int StatusHandler(struct mg_connection* conn, void* data) {
    auto* state = static_cast<ServerState*>(data);
    state->request_count_.fetch_add(1);
    SendJson(conn, 200, state->StatusJson());
    return 200;
}

int ShutdownHandler(struct mg_connection* conn, void* data) {
    auto* state = static_cast<ServerState*>(data);
    state->request_count_.fetch_add(1);
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
    state->request_count_.fetch_add(1);
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

    std::string error;
    if (!state->Synthesize(request, &error)) {
        SendJson(conn, 500, {{"ok", false}, {"error", error}, {"status", state->StatusJson()}});
        return 500;
    }

    SendJson(conn, 200, {{"ok", true}, {"status", state->StatusJson()}});
    return 200;
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
        } else if (arg == "--piper") {
            config.piper_path = NextArg(i, argc, argv);
        } else if (arg == "--model") {
            config.model_path = NextArg(i, argc, argv);
        } else if (arg == "--config") {
            config.config_path = NextArg(i, argc, argv);
        } else if (arg == "--models-dir") {
            config.models_dir = NextArg(i, argc, argv);
        } else if (arg == "--log-dir") {
            config.log_dir = NextArg(i, argc, argv);
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
    if (config.log_dir.empty()) {
        config.log_dir = DefaultLogDirectory().string();
    }
    if (config.piper_path.empty()) {
#ifdef _WIN32
        config.piper_path = (std::filesystem::path(argv[0]).parent_path() / "piper.exe").string();
#else
        config.piper_path = (std::filesystem::path(argv[0]).parent_path() / "piper").string();
#endif
    }
    return config;
}

} // namespace

int main(int argc, char** argv) {
    ServerConfig config = ParseArgs(argc, argv);
    ServerState state(config);
    state.Log("startup: parsed command line");
    state.Log("startup: using Piper executable: " + config.piper_path);

    if (!mg_init_library(MG_FEATURES_DEFAULT)) {
        state.Log("startup: failed to initialize CivetWeb");
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
        state.Log("startup: failed to start textlt-piper-server on " + listen);
        mg_exit_library();
        return 2;
    }

    mg_set_request_handler(context, "/health", HealthHandler, &state);
    mg_set_request_handler(context, "/status", StatusHandler, &state);
    mg_set_request_handler(context, "/shutdown", ShutdownHandler, &state);
    mg_set_request_handler(context, "/synthesize", SynthesizeHandler, &state);

    state.Log("startup: textlt-piper-server listening on " + listen);
    state.PreloadInitialVoice();

    while (!state.shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    state.Log("shutdown: stopping textlt-piper-server");
    mg_stop(context);
    mg_exit_library();
    return 0;
}
