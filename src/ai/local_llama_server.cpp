#include "ai/local_llama_server.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include "ai/ai_model_catalog.hpp"
#include "json_utils.hpp"
#include "remote/remote_http_client.hpp"

namespace textlt {
namespace {

std::filesystem::path UserDataDirectory() {
#ifdef _WIN32
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data && *local_app_data) {
        return std::filesystem::path(local_app_data) / "textlt";
    }
    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && *user_profile) {
        return std::filesystem::path(user_profile) / "AppData" / "Local" / "textlt";
    }
    return std::filesystem::path(".") / "textlt-data";
#else
    const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home && *xdg_data_home) {
        return std::filesystem::path(xdg_data_home) / "textlt";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home) / ".local" / "share" / "textlt";
    }
    return std::filesystem::path(".") / ".textlt";
#endif
}

bool SafeFilename(const std::string& filename) {
    const std::filesystem::path path(filename);
    return !filename.empty() && !path.is_absolute() && path.parent_path().empty() &&
        path.filename() == path && filename != "." && filename != "..";
}

std::filesystem::path FindServerBinary() {
    const std::filesystem::path root =
        UserDataDirectory() / "ai" / "runtimes" / "llama_cpp";
#ifdef _WIN32
    const std::string wanted = "llama-server.exe";
#else
    const std::string wanted = "llama-server";
#endif
    std::error_code error;
    if (!std::filesystem::exists(root, error)) {
        return {};
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
        if (error) {
            return {};
        }
        if (entry.is_regular_file() && entry.path().filename() == wanted) {
            return entry.path();
        }
    }
    return {};
}

int ClampThreads(int threads) {
    if (threads > 0) {
        return std::clamp(threads, 1, 16);
    }
    const unsigned int available = std::thread::hardware_concurrency();
    if (available <= 2) {
        return 1;
    }
    return static_cast<int>(std::min<unsigned int>(6, std::max<unsigned int>(2, available / 2)));
}

bool IsGpuCatalogModel(const std::string& filename) {
    const BuiltInAiModel* model = FindBuiltInAiModel(filename);
    return model && model->gpu_required;
}

struct GpuProbeResult {
    bool available = false;
    std::string device;
    int total_memory_mb = 0;
    int free_memory_mb = 0;
    std::string error;
};

bool ServerReportsModel(const std::string& body, const std::string& model_filename) {
    const Json root = Json::parse(body, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return false;
    }
    const auto data = root.find("data");
    if (data == root.end() || !data->is_array()) {
        return false;
    }
    for (const Json& item : *data) {
        if (item.is_object() && JsonString(item, "id") == model_filename) {
            return true;
        }
    }
    return false;
}

GpuProbeResult ProbeGpuDevice(
    const std::filesystem::path& server,
    const std::atomic<bool>* cancel_requested,
    RemoteCommandControl* command_control) {
    RemoteCommandRunner runner;
    const RemoteCommandResult result = runner.RunStreaming(
        {server.string(), "--list-devices"},
        {},
        cancel_requested,
        {},
        RemoteCommandOptions{10, 300, true},
        command_control);
    if (result.cancelled) {
        GpuProbeResult probe;
        probe.error = "GPU detection stopped.";
        return probe;
    }
    if (result.timed_out) {
        GpuProbeResult probe;
        probe.error = "GPU detection timed out. Check the llama.cpp GPU runtime and driver.";
        return probe;
    }
    if (result.exit_code != 0) {
        GpuProbeResult probe;
        probe.error = result.error.empty()
            ? "The installed llama-server cannot list GPU devices. Update the llama.cpp runtime."
            : "GPU detection failed: " + result.error;
        return probe;
    }

    const std::regex device_pattern(
        R"(^\s*((?:CUDA|Vulkan|ROCm|Metal|SYCL|MUSA|CANN|Kompute)[0-9]*):\s*(.+?)\s*$)",
        std::regex::icase);
    const std::regex memory_pattern(
        R"(\(([0-9]+)\s+MiB(?:,\s*([0-9]+)\s+MiB\s+free)?\))",
        std::regex::icase);
    std::istringstream lines(result.output + "\n" + result.error);
    std::string line;
    GpuProbeResult best;
    while (std::getline(lines, line)) {
        std::smatch device_match;
        if (!std::regex_match(line, device_match, device_pattern)) {
            continue;
        }
        GpuProbeResult candidate;
        candidate.available = true;
        std::string device_details = device_match[2].str();
        std::smatch memory_match;
        if (std::regex_search(device_details, memory_match, memory_pattern)) {
            candidate.total_memory_mb = std::stoi(memory_match[1].str());
            if (memory_match[2].matched) {
                candidate.free_memory_mb = std::stoi(memory_match[2].str());
            }
            device_details.erase(static_cast<size_t>(memory_match.position()));
            while (!device_details.empty() &&
                   std::isspace(static_cast<unsigned char>(device_details.back()))) {
                device_details.pop_back();
            }
        }
        candidate.device = device_match[1].str() + ": " + device_details;
        const int candidate_memory = candidate.free_memory_mb > 0
            ? candidate.free_memory_mb
            : candidate.total_memory_mb;
        const int best_memory = best.free_memory_mb > 0
            ? best.free_memory_mb
            : best.total_memory_mb;
        if (!best.available || candidate_memory > best_memory) {
            best = std::move(candidate);
        }
    }
    if (!best.available) {
        best.error =
            "This model requires a GPU-enabled llama.cpp runtime, but llama-server reported no compatible GPU device.";
    }
    return best;
}

} // namespace

LocalLlamaServerManager& LocalLlamaServerManager::Instance() {
    static LocalLlamaServerManager manager;
    return manager;
}

LocalLlamaServerManager::~LocalLlamaServerManager() {
    Unload();
}

int LocalLlamaServerManager::ServerPort() {
    const char* configured = std::getenv("TEXTLT_LLAMA_SERVER_PORT");
    if (configured && *configured) {
        try {
            const int port = std::stoi(configured);
            if (port >= 1024 && port <= 65535) {
                return port;
            }
        } catch (...) {
        }
    }
    return 11435;
}

std::string LocalLlamaServerManager::BaseUrl() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return snapshot_.base_url.empty()
        ? "http://127.0.0.1:" + std::to_string(ServerPort())
        : snapshot_.base_url;
}

bool LocalLlamaServerManager::WaitUntilIdle(int timeout_milliseconds) const {
    const std::string base_url = BaseUrl();
    RemoteHttpClient client;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(std::max(0, timeout_milliseconds));
    do {
        const RemoteHttpResponse response = client.Request(
            "GET", base_url + "/slots", {"Accept: application/json"}, {}, 1);
        if (response.ok) {
            const Json slots = Json::parse(response.body, nullptr, false);
            if (slots.is_array()) {
                bool processing = false;
                for (const Json& slot : slots) {
                    if (slot.is_object() && slot.value("is_processing", false)) {
                        processing = true;
                        break;
                    }
                }
                if (!processing) {
                    return true;
                }
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
    } while (true);
    return false;
}

bool LocalLlamaServerManager::RuntimeAvailable() const {
    return !FindServerBinary().empty();
}

std::string LocalLlamaServerManager::RuntimePath() const {
    return FindServerBinary().string();
}

bool LocalLlamaServerManager::CheckModelHardware(
    const std::string& model_filename,
    std::string& error,
    const std::atomic<bool>* cancel_requested,
    RemoteCommandControl* command_control) const {
    if (!IsGpuCatalogModel(model_filename)) {
        return true;
    }
    const std::filesystem::path server = FindServerBinary();
    if (server.empty()) {
        error = "llama-server is not installed. Download a current llama.cpp runtime.";
        return false;
    }
    const GpuProbeResult gpu = ProbeGpuDevice(server, cancel_requested, command_control);
    if (!gpu.available) {
        error = gpu.error;
        return false;
    }
    return true;
}

LocalLlamaServerSnapshot LocalLlamaServerManager::Snapshot() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return snapshot_;
}

bool LocalLlamaServerManager::IsReadyFor(const std::string& model_filename) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return snapshot_.state == LocalLlamaServerState::Ready &&
        snapshot_.model_filename == model_filename;
}

bool LocalLlamaServerManager::EnsureRunning(
    const std::string& model_filename,
    int threads,
    int startup_timeout_seconds,
    std::string& error,
    const std::atomic<bool>* cancel_requested) {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (snapshot_.state == LocalLlamaServerState::Ready &&
            snapshot_.model_filename == model_filename) {
            return true;
        }
    }
    StopLocked();
    return StartLocked(
        model_filename, threads, startup_timeout_seconds, error, cancel_requested);
}

bool LocalLlamaServerManager::Restart(
    const std::string& model_filename,
    int threads,
    int startup_timeout_seconds,
    std::string& error,
    const std::atomic<bool>* cancel_requested) {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    StopLocked();
    return StartLocked(
        model_filename, threads, startup_timeout_seconds, error, cancel_requested);
}

bool LocalLlamaServerManager::StartLocked(
    const std::string& model_filename,
    int threads,
    int startup_timeout_seconds,
    std::string& error,
    const std::atomic<bool>* cancel_requested) {
    if (!SafeFilename(model_filename)) {
        error = "Selected local model filename is invalid.";
        return false;
    }
    const std::filesystem::path server = FindServerBinary();
    if (server.empty()) {
        error = "llama-server is not installed. Download a current llama.cpp runtime.";
        return false;
    }
    const std::filesystem::path model = UserDataDirectory() / "ai" / "models" / model_filename;
    std::error_code fs_error;
    if (!std::filesystem::is_regular_file(model, fs_error)) {
        error = "Selected local model is not downloaded: " + model_filename;
        return false;
    }

    server_control_.Reset();
    GpuProbeResult gpu;
    if (IsGpuCatalogModel(model_filename)) {
        gpu = ProbeGpuDevice(server, cancel_requested, &server_control_);
        if (!gpu.available) {
            error = gpu.error;
            return false;
        }
    }

    const auto load_started = std::chrono::steady_clock::now();
    const int port = ServerPort();
    const std::string base_url = "http://127.0.0.1:" + std::to_string(port);
    const int thread_count = ClampThreads(threads);
    std::vector<std::string> arguments = {
        server.string(),
        "--model", model.string(),
        "--alias", model_filename,
        "--host", "127.0.0.1",
        "--port", std::to_string(port),
        "--ctx-size", "4096",
        "--threads", std::to_string(thread_count),
        "--threads-batch", std::to_string(thread_count),
        "--parallel", "1",
        "--jinja",
        "--no-mmproj",
    };

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        snapshot_.state = LocalLlamaServerState::Starting;
        snapshot_.model_filename = model_filename;
        snapshot_.base_url = base_url;
        snapshot_.status = "Loading local model into llama-server...";
        snapshot_.gpu_device = gpu.device;
        snapshot_.gpu_total_memory_mb = gpu.total_memory_mb;
        snapshot_.gpu_free_memory_mb = gpu.free_memory_mb;
    }

    server_thread_ = std::thread([this, arguments] {
        RemoteCommandRunner runner;
        const RemoteCommandResult result = runner.RunStreaming(
            arguments,
            {},
            nullptr,
            {},
            RemoteCommandOptions{0, 500, true},
            &server_control_);
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (snapshot_.state == LocalLlamaServerState::Stopping || result.cancelled) {
            snapshot_.state = LocalLlamaServerState::Stopped;
            snapshot_.status = "Local model unloaded.";
        } else if (snapshot_.state != LocalLlamaServerState::Ready) {
            snapshot_.state = LocalLlamaServerState::Failed;
            snapshot_.status = result.error.empty()
                ? "llama-server exited before the model became ready."
                : result.error;
        } else {
            snapshot_.state = LocalLlamaServerState::Failed;
            snapshot_.status = "llama-server stopped unexpectedly.";
        }
    });

    RemoteHttpClient client;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(std::max(10, startup_timeout_seconds));
    while (std::chrono::steady_clock::now() < deadline) {
        if ((cancel_requested && cancel_requested->load()) ||
            server_control_.StopRequested()) {
            error = cancel_requested && cancel_requested->load()
                ? "AI task cancelled while the local model was loading."
                : "Local model loading was cancelled by Unload Model.";
            StopLocked();
            return false;
        }
        std::string startup_error;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (snapshot_.state == LocalLlamaServerState::Failed) {
                startup_error = snapshot_.status;
            }
        }
        if (!startup_error.empty()) {
            error = startup_error;
            StopLocked();
            return false;
        }
        const RemoteHttpResponse health = client.Request("GET", base_url + "/health", {}, {}, 1);
        if (health.status_code == 200) {
            const RemoteHttpResponse models = client.Request(
                "GET", base_url + "/v1/models", {"Accept: application/json"}, {}, 2);
            if (models.ok && ServerReportsModel(models.body, model_filename)) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                snapshot_.state = LocalLlamaServerState::Ready;
                snapshot_.status = snapshot_.gpu_device.empty()
                    ? "Local model loaded and ready."
                    : "Local model loaded on " + snapshot_.gpu_device + ".";
                snapshot_.load_ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - load_started).count();
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    error = "Timed out while loading the local model in llama-server.";
    StopLocked();
    return false;
}

void LocalLlamaServerManager::StopLocked() {
    bool join = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        join = server_thread_.joinable();
        if (join) {
            snapshot_.state = LocalLlamaServerState::Stopping;
            snapshot_.status = "Unloading local model...";
        }
    }
    if (join) {
        server_control_.RequestStop();
        server_thread_.join();
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    snapshot_.state = LocalLlamaServerState::Stopped;
    snapshot_.model_filename.clear();
    snapshot_.base_url = "http://127.0.0.1:" + std::to_string(ServerPort());
    snapshot_.status = "Local model is not loaded.";
    snapshot_.gpu_device.clear();
    snapshot_.gpu_total_memory_mb = 0;
    snapshot_.gpu_free_memory_mb = 0;
    snapshot_.load_ms = 0.0;
}

void LocalLlamaServerManager::Unload() {
    server_control_.RequestStop();
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    StopLocked();
}

} // namespace textlt
