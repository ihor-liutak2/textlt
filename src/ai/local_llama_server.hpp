#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "remote/remote_command_runner.hpp"

namespace textlt {

enum class LocalLlamaServerState {
    Stopped,
    Starting,
    Ready,
    Stopping,
    Failed,
};

struct LocalLlamaServerSnapshot {
    LocalLlamaServerState state = LocalLlamaServerState::Stopped;
    std::string model_filename;
    std::string base_url;
    std::string status = "Local model is not loaded.";
    std::string gpu_device;
    int gpu_total_memory_mb = 0;
    int gpu_free_memory_mb = 0;
    double load_ms = 0.0;
};

class LocalLlamaServerManager {
public:
    static LocalLlamaServerManager& Instance();

    LocalLlamaServerManager(const LocalLlamaServerManager&) = delete;
    LocalLlamaServerManager& operator=(const LocalLlamaServerManager&) = delete;

    bool EnsureRunning(
        const std::string& model_filename,
        int threads,
        int startup_timeout_seconds,
        std::string& error,
        const std::atomic<bool>* cancel_requested = nullptr);
    bool Restart(
        const std::string& model_filename,
        int threads,
        int startup_timeout_seconds,
        std::string& error,
        const std::atomic<bool>* cancel_requested = nullptr);
    void Unload();

    LocalLlamaServerSnapshot Snapshot() const;
    bool IsReadyFor(const std::string& model_filename) const;
    std::string BaseUrl() const;
    bool WaitUntilIdle(int timeout_milliseconds) const;
    bool RuntimeAvailable() const;
    std::string RuntimePath() const;
    bool CheckModelHardware(
        const std::string& model_filename,
        std::string& error,
        const std::atomic<bool>* cancel_requested = nullptr,
        RemoteCommandControl* command_control = nullptr) const;

private:
    LocalLlamaServerManager() = default;
    ~LocalLlamaServerManager();

    bool StartLocked(
        const std::string& model_filename,
        int threads,
        int startup_timeout_seconds,
        std::string& error,
        const std::atomic<bool>* cancel_requested);
    void StopLocked();
    static int ServerPort();

    mutable std::mutex state_mutex_;
    std::mutex lifecycle_mutex_;
    std::thread server_thread_;
    RemoteCommandControl server_control_;
    LocalLlamaServerSnapshot snapshot_;
};

} // namespace textlt
