#include "remote/remote_command_runner.hpp"

#include <algorithm>
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

void CloseWindowsHandle(HANDLE& handle) {
    if (handle) {
        CloseHandle(handle);
        handle = nullptr;
    }
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
void IgnoreSigpipeForProcess() {
    static std::once_flag once;
    std::call_once(once, [] {
        struct sigaction action {};
        action.sa_handler = SIG_IGN;
        sigemptyset(&action.sa_mask);
        sigaction(SIGPIPE, &action, nullptr);
    });
}

void CloseFd(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

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

void RemoteCommandControl::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
        stop_requested_.store(false);
        process_handle_ = 0;
        group_handle_ = 0;
    }
}

void RemoteCommandControl::RequestStop() {
    stop_requested_.store(true);
    TerminateAttached(false);
}

bool RemoteCommandControl::IsRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

bool RemoteCommandControl::StopRequested() const {
    return stop_requested_.load();
}

void RemoteCommandControl::Attach(
    std::intptr_t process_handle,
    std::intptr_t group_handle) {
    bool stop_now = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        process_handle_ = process_handle;
        group_handle_ = group_handle;
        running_ = true;
        stop_now = stop_requested_.load();
    }
    if (stop_now) {
        TerminateAttached(false);
    }
}

void RemoteCommandControl::Detach() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    process_handle_ = 0;
    group_handle_ = 0;
}

void RemoteCommandControl::TerminateAttached(bool force) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || process_handle_ == 0) {
        return;
    }
#ifdef _WIN32
    HANDLE process = reinterpret_cast<HANDLE>(process_handle_);
    HANDLE job = reinterpret_cast<HANDLE>(group_handle_);
    if (job) {
        TerminateJobObject(job, force ? 137 : 130);
    } else if (process) {
        TerminateProcess(process, force ? 137 : 130);
    }
#else
    const pid_t process = static_cast<pid_t>(process_handle_);
    const pid_t group = static_cast<pid_t>(group_handle_);
    const int signal = force ? SIGKILL : SIGTERM;
    if (group > 0) {
        if (kill(-group, signal) == 0 || errno != ESRCH) {
            return;
        }
    }
    kill(process, signal);
#endif
}

RemoteCommandResult RemoteCommandRunner::RunStreaming(
    const std::vector<std::string>& args,
    const std::string& stdin_text,
    const std::atomic<bool>* cancel_requested,
    OutputCallback on_stdout,
    RemoteCommandOptions options,
    RemoteCommandControl* control) const {
    RemoteCommandResult result;
    if (args.empty()) {
        result.error = "No command specified.";
        return result;
    }
    options.timeout_seconds = std::max(0, options.timeout_seconds);
    options.terminate_grace_ms = std::max(0, options.terminate_grace_ms);
    const auto started_at = std::chrono::steady_clock::now();

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
        CloseWindowsHandle(stdout_read);
        CloseWindowsHandle(stdout_write);
        CloseWindowsHandle(stderr_read);
        CloseWindowsHandle(stderr_write);
        CloseWindowsHandle(stdin_read);
        CloseWindowsHandle(stdin_write);
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

    DWORD creation_flags = CREATE_NO_WINDOW;
    if (options.create_process_group) {
        creation_flags |= CREATE_NEW_PROCESS_GROUP;
    }
    const BOOL created = CreateProcessA(
        nullptr,
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        creation_flags,
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

    HANDLE job = nullptr;
    if (options.create_process_group) {
        job = CreateJobObjectA(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(
                    job,
                    JobObjectExtendedLimitInformation,
                    &limits,
                    sizeof(limits)) ||
                !AssignProcessToJobObject(job, process.hProcess)) {
                CloseHandle(job);
                job = nullptr;
            }
        }
    }
    if (control) {
        control->Attach(
            reinterpret_cast<std::intptr_t>(process.hProcess),
            reinterpret_cast<std::intptr_t>(job));
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

    bool stop_started = false;
    auto stop_sent_at = std::chrono::steady_clock::time_point{};
    while (true) {
        DrainWindowsPipe(stdout_read, result.output, on_stdout);
        DrainWindowsPipe(stderr_read, result.error, {});
        const auto now = std::chrono::steady_clock::now();
        const bool timeout = options.timeout_seconds > 0 &&
            now - started_at >= std::chrono::seconds(options.timeout_seconds);
        const bool cancelled =
            (cancel_requested && cancel_requested->load()) ||
            (control && control->StopRequested());
        if ((timeout || cancelled) && !stop_started) {
            stop_started = true;
            stop_sent_at = now;
            result.timed_out = timeout;
            result.cancelled = !timeout;
            if (control) {
                control->RequestStop();
            } else if (job) {
                TerminateJobObject(job, timeout ? 124 : 130);
            } else {
                TerminateProcess(process.hProcess, timeout ? 124 : 130);
            }
        } else if (stop_started &&
                   now - stop_sent_at >= std::chrono::milliseconds(options.terminate_grace_ms)) {
            if (control) {
                control->TerminateAttached(true);
            }
        }
        const DWORD wait = WaitForSingleObject(process.hProcess, 40);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) {
            break;
        }
    }
    DrainWindowsPipe(stdout_read, result.output, on_stdout);
    DrainWindowsPipe(stderr_read, result.error, {});
    DWORD exit_code = static_cast<DWORD>(-1);
    GetExitCodeProcess(process.hProcess, &exit_code);
    if (control) {
        if (control->StopRequested() && !result.timed_out) {
            result.cancelled = true;
        }
        control->Detach();
    }
    result.exit_code = result.timed_out ? 124 : (result.cancelled ? 130 : static_cast<int>(exit_code));
    if (result.timed_out) {
        result.error = "Operation timed out after " +
            std::to_string(options.timeout_seconds) + " seconds.";
    } else if (result.cancelled && result.error.empty()) {
        result.error = "Operation stopped.";
    }
    CloseHandle(stdout_read);
    CloseHandle(stderr_read);
    if (job) {
        CloseHandle(job);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int stdin_pipe[2] = {-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0 || pipe(stdin_pipe) != 0) {
        CloseFd(stdout_pipe[0]);
        CloseFd(stdout_pipe[1]);
        CloseFd(stderr_pipe[0]);
        CloseFd(stderr_pipe[1]);
        CloseFd(stdin_pipe[0]);
        CloseFd(stdin_pipe[1]);
        result.error = "Cannot create process pipes.";
        return result;
    }

    const pid_t child = fork();
    if (child < 0) {
        CloseFd(stdout_pipe[0]);
        CloseFd(stdout_pipe[1]);
        CloseFd(stderr_pipe[0]);
        CloseFd(stderr_pipe[1]);
        CloseFd(stdin_pipe[0]);
        CloseFd(stdin_pipe[1]);
        result.error = "Cannot start command.";
        return result;
    }
    if (child == 0) {
        if (options.create_process_group) {
            setpgid(0, 0);
        }
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

    if (options.create_process_group) {
        setpgid(child, child);
    }
    if (control) {
        control->Attach(
            static_cast<std::intptr_t>(child),
            options.create_process_group ? static_cast<std::intptr_t>(child) : 0);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    IgnoreSigpipeForProcess();
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
    bool stop_started = false;
    int wait_status = 0;
    auto stop_sent_at = std::chrono::steady_clock::time_point{};
    auto terminate = [&](bool force) {
        if (control) {
            control->TerminateAttached(force);
            return;
        }
        const int signal = force ? SIGKILL : SIGTERM;
        if (options.create_process_group) {
            if (kill(-child, signal) == 0 || errno != ESRCH) {
                return;
            }
        }
        kill(child, signal);
    };

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

        const auto now = std::chrono::steady_clock::now();
        const bool timeout = options.timeout_seconds > 0 &&
            now - started_at >= std::chrono::seconds(options.timeout_seconds);
        const bool cancelled =
            (cancel_requested && cancel_requested->load()) ||
            (control && control->StopRequested());
        if (running && (timeout || cancelled)) {
            if (!stop_started) {
                stop_started = true;
                stop_sent_at = now;
                result.timed_out = timeout;
                result.cancelled = !timeout;
                if (control && !control->StopRequested()) {
                    control->RequestStop();
                } else {
                    terminate(false);
                }
            } else if (now - stop_sent_at >=
                       std::chrono::milliseconds(options.terminate_grace_ms)) {
                terminate(true);
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
    if (control) {
        if (control->StopRequested() && !result.timed_out) {
            result.cancelled = true;
        }
        control->Detach();
    }
    result.exit_code = result.timed_out ? 124 :
        (result.cancelled ? 130 : DecodeSystemExitCode(wait_status));
    if (result.timed_out) {
        result.error = "Operation timed out after " +
            std::to_string(options.timeout_seconds) + " seconds.";
    } else if (result.cancelled && result.error.empty()) {
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
