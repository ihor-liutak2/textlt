#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace textlt {

struct RemoteCommandResult {
    int exit_code = -1;
    std::string output;
    std::string error;
    bool cancelled = false;
    bool timed_out = false;
};

struct RemoteCommandOptions {
    int timeout_seconds = 0;
    int terminate_grace_ms = 300;
    bool create_process_group = true;
    int idle_timeout_seconds = 0;
};

class RemoteCommandControl {
public:
    RemoteCommandControl() = default;
    RemoteCommandControl(const RemoteCommandControl&) = delete;
    RemoteCommandControl& operator=(const RemoteCommandControl&) = delete;

    void Reset();
    void RequestStop();
    bool IsRunning() const;
    bool StopRequested() const;

private:
    friend class RemoteCommandRunner;

    void Attach(std::intptr_t process_handle, std::intptr_t group_handle);
    void Detach();
    void TerminateAttached(bool force);

    mutable std::mutex mutex_;
    std::atomic<bool> stop_requested_{false};
    bool running_ = false;
    std::intptr_t process_handle_ = 0;
    std::intptr_t group_handle_ = 0;
};

class RemoteCommandRunner {
public:
    using OutputCallback = std::function<void(const std::string& chunk)>;

    RemoteCommandResult Run(
        const std::vector<std::string>& args,
        const std::string& stdin_text = {}) const;

    RemoteCommandResult RunStreaming(
        const std::vector<std::string>& args,
        const std::string& stdin_text,
        const std::atomic<bool>* cancel_requested,
        OutputCallback on_stdout,
        RemoteCommandOptions options = {},
        RemoteCommandControl* control = nullptr) const;

    static std::string ShellQuote(const std::string& value);

private:
    static std::filesystem::path MakeTempPath(const std::string& prefix);
    static bool WriteTextFile(const std::filesystem::path& path, const std::string& text);
    static std::string ReadTextFile(const std::filesystem::path& path);
};

} // namespace textlt
