#include "remote/remote_command_runner.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <system_error>
#include <thread>
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace textlt {
namespace {

std::string JoinShellCommand(const std::vector<std::string>& args) {
    std::string command;
    for (const std::string& arg : args) {
        if (!command.empty()) {
            command += ' ';
        }
        command += RemoteCommandRunner::ShellQuote(arg);
    }
    return command;
}

int DecodeSystemExitCode(int result) {
    if (result == -1) {
        return -1;
    }
#ifdef _WIN32
    return result;
#else
    if (WIFEXITED(result)) {
        return WEXITSTATUS(result);
    }
    if (WIFSIGNALED(result)) {
        return 128 + WTERMSIG(result);
    }
    return result;
#endif
}

} // namespace

RemoteCommandResult RemoteCommandRunner::Run(
    const std::vector<std::string>& args,
    const std::string& stdin_text) const {
    RemoteCommandResult result;
    if (args.empty()) {
        result.error = "No command specified.";
        return result;
    }

    const std::filesystem::path stdout_path = MakeTempPath("textlt-remote-out");
    const std::filesystem::path stderr_path = MakeTempPath("textlt-remote-err");
    const std::filesystem::path stdin_path = MakeTempPath("textlt-remote-in");

    if (!stdin_text.empty() && !WriteTextFile(stdin_path, stdin_text)) {
        result.error = "Cannot write temporary command input.";
        return result;
    }

    std::string command = JoinShellCommand(args);
    if (!stdin_text.empty()) {
        command += " < " + ShellQuote(stdin_path.string());
    }
    command += " > " + ShellQuote(stdout_path.string());
    command += " 2> " + ShellQuote(stderr_path.string());

    result.exit_code = DecodeSystemExitCode(std::system(command.c_str()));
    result.output = ReadTextFile(stdout_path);
    result.error = ReadTextFile(stderr_path);

    std::error_code remove_error;
    std::filesystem::remove(stdout_path, remove_error);
    std::filesystem::remove(stderr_path, remove_error);
    std::filesystem::remove(stdin_path, remove_error);
    return result;
}

std::string RemoteCommandRunner::ShellQuote(const std::string& value) {
    if (value.empty()) {
#ifdef _WIN32
        return "\"\"";
#else
        return "''";
#endif
    }
#ifdef _WIN32
    // Windows cmd.exe uses double quotes for escaping
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\"\"";
        } else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
#else
    // Unix shells use single quotes for escaping
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
#endif
}

std::filesystem::path RemoteCommandRunner::MakeTempPath(const std::string& prefix) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    const auto thread_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::filesystem::temp_directory_path() /
        (prefix + "-" + std::to_string(ticks) + "-" + std::to_string(thread_hash));
}

bool RemoteCommandRunner::WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << text;
    return static_cast<bool>(file);
}

std::string RemoteCommandRunner::ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

} // namespace textlt
