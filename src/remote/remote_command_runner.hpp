#pragma once

#include <filesystem>
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
    RemoteCommandResult Run(
        const std::vector<std::string>& args,
        const std::string& stdin_text = {}) const;

    static std::string ShellQuote(const std::string& value);

private:
    static std::filesystem::path MakeTempPath(const std::string& prefix);
    static bool WriteTextFile(const std::filesystem::path& path, const std::string& text);
    static std::string ReadTextFile(const std::filesystem::path& path);
};

} // namespace textlt
