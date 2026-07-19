#include "remote/remote_command_runner.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <system_error>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
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

#ifdef _WIN32
std::string WindowsQuoteArgument(const std::string& value) {
    if (value.empty()) {
        return "\"\"";
    }
    if (value.find_first_of(" \t\n\v\"") == std::string::npos) {
        return value;
    }
    std::string result = "\"";
    size_t backslashes = 0;
    for (char ch : value) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '"') {
            result.append(backslashes * 2 + 1, '\\');
            result.push_back('"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, '\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, '\\');
    result.push_back('"');
    return result;
}

std::string WindowsCommandLine(const std::vector<std::string>& args) {
    std::string command;
    for (const std::string& arg : args) {
        if (!command.empty()) {
            command.push_back(' ');
        }
        command += WindowsQuoteArgument(arg);
    }
    return command;
}

void DrainWindowsPipe(
    HANDLE pipe,
    std::string& destination,
    const RemoteCommandRunner::OutputCallback& callback) {
    while (true) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
            return;
        }
        char buffer[4096];
        DWORD read = 0;
        const DWORD requested = std::min<DWORD>(available, sizeof(buffer));
        if (!ReadFile(pipe, buffer, requested, &read, nullptr) || read == 0) {
            return;
        }
        std::string chunk(buffer, buffer + read);
        destination += chunk;
        if (callback) {
            callback(chunk);
        }
    }
}
#else
void SetNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

bool DrainPosixFd(
    int fd,
    std::string& destination,
    const RemoteCommandRunner::OutputCallback& callback) {
    char buffer[4096];
    while (true) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            std::string chunk(buffer, buffer + count);
            destination += chunk;
            if (callback) {
                callback(chunk);
            }
            continue;
        }
        if (count == 0) {
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return true;
        }
        return false;
    }
}
#endif

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

RemoteCommandResult RemoteCommandRunner::RunStreaming(
    const std::vector<std::string>& args,
    const std::string& stdin_text,
    const std::atomic<bool>* cancel_requested,
    OutputCallback on_stdout) const {
    RemoteCommandResult result;
    if (args.empty()) {
        result.error = "No command specified.";
        return result;
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;
    HANDLE stdin_read = nullptr;
    HANDLE stdin_write = nullptr;
    if (!CreatePipe(&stdout_read, &stdout_write, &security, 0) ||
        !CreatePipe(&stderr_read, &stderr_write, &security, 0) ||
        !CreatePipe(&stdin_read, &stdin_write, &security, 0)) {
        result.error = "Cannot create process pipes.";
        return result;
    }
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_read;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stderr_write;
    PROCESS_INFORMATION process{};
    std::string command_line = WindowsCommandLine(args);
    std::vector<char> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back('\0');

    const BOOL created = CreateProcessA(
        nullptr,
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process);
    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    CloseHandle(stderr_write);
    if (!created) {
        CloseHandle(stdin_write);
        CloseHandle(stdout_read);
        CloseHandle(stderr_read);
        result.error = "Cannot start command. Windows error " +
            std::to_string(GetLastError()) + ".";
        return result;
    }

    if (!stdin_text.empty()) {
        DWORD written = 0;
        WriteFile(
            stdin_write,
            stdin_text.data(),
            static_cast<DWORD>(stdin_text.size()),
            &written,
            nullptr);
    }
    CloseHandle(stdin_write);

    bool stopped = false;
    while (true) {
        DrainWindowsPipe(stdout_read, result.output, on_stdout);
        DrainWindowsPipe(stderr_read, result.error, {});
        if (cancel_requested && cancel_requested->load()) {
            TerminateProcess(process.hProcess, 130);
            stopped = true;
        }
        const DWORD wait = WaitForSingleObject(process.hProcess, 40);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (wait == WAIT_FAILED) {
            break;
        }
    }
    DrainWindowsPipe(stdout_read, result.output, on_stdout);
    DrainWindowsPipe(stderr_read, result.error, {});
    DWORD exit_code = static_cast<DWORD>(-1);
    GetExitCodeProcess(process.hProcess, &exit_code);
    result.exit_code = stopped ? 130 : static_cast<int>(exit_code);
    if (stopped && result.error.empty()) {
        result.error = "Operation stopped.";
    }
    CloseHandle(stdout_read);
    CloseHandle(stderr_read);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int stdin_pipe[2] = {-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0 || pipe(stdin_pipe) != 0) {
        result.error = "Cannot create process pipes.";
        return result;
    }

    const pid_t child = fork();
    if (child < 0) {
        result.error = "Cannot start command.";
        return result;
    }
    if (child == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        const char* message = "Cannot execute command.\n";
        write(STDERR_FILENO, message, std::strlen(message));
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    if (!stdin_text.empty()) {
        size_t offset = 0;
        while (offset < stdin_text.size()) {
            const ssize_t count = write(
                stdin_pipe[1],
                stdin_text.data() + offset,
                stdin_text.size() - offset);
            if (count > 0) {
                offset += static_cast<size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
    }
    close(stdin_pipe[1]);
    SetNonBlocking(stdout_pipe[0]);
    SetNonBlocking(stderr_pipe[0]);

    bool stdout_open = true;
    bool stderr_open = true;
    bool running = true;
    bool stopped = false;
    int wait_status = 0;
    auto stop_sent_at = std::chrono::steady_clock::time_point{};
    while (running || stdout_open || stderr_open) {
        pollfd descriptors[2]{};
        nfds_t descriptor_count = 0;
        if (stdout_open) {
            descriptors[descriptor_count++] = {stdout_pipe[0], POLLIN | POLLHUP, 0};
        }
        if (stderr_open) {
            descriptors[descriptor_count++] = {stderr_pipe[0], POLLIN | POLLHUP, 0};
        }
        if (descriptor_count > 0) {
            poll(descriptors, descriptor_count, 40);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }

        stdout_open = stdout_open && DrainPosixFd(stdout_pipe[0], result.output, on_stdout);
        stderr_open = stderr_open && DrainPosixFd(stderr_pipe[0], result.error, {});

        if (running && cancel_requested && cancel_requested->load()) {
            if (!stopped) {
                kill(child, SIGTERM);
                stopped = true;
                stop_sent_at = std::chrono::steady_clock::now();
            } else if (std::chrono::steady_clock::now() - stop_sent_at >
                       std::chrono::milliseconds(300)) {
                kill(child, SIGKILL);
            }
        }

        if (running) {
            const pid_t waited = waitpid(child, &wait_status, WNOHANG);
            if (waited == child) {
                running = false;
            } else if (waited < 0 && errno != EINTR) {
                running = false;
            }
        }
    }
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    if (running) {
        waitpid(child, &wait_status, 0);
    }
    result.exit_code = stopped ? 130 : DecodeSystemExitCode(wait_status);
    if (stopped && result.error.empty()) {
        result.error = "Operation stopped.";
    }
#endif
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
