#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace textlt {

struct RemoteCommandResult {
    int exit_code = -1;
    std::string output;
    std::string error;
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
        OutputCallback on_stdout) const;

    static std::string ShellQuote(const std::string& value);

private:
    static std::filesystem::path MakeTempPath(const std::string& prefix);
    static bool WriteTextFile(const std::filesystem::path& path, const std::string& text);
    static std::string ReadTextFile(const std::filesystem::path& path);
};

} // namespace textlt
