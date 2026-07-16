#include "remote/remote_sftp_provider.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace textlt {
namespace {

std::string Trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

bool SplitFindLine(const std::string& line, char& type, std::uintmax_t& size, std::string& name) {
    const size_t first_tab = line.find('\t');
    if (first_tab == std::string::npos || first_tab == 0) {
        return false;
    }
    const size_t second_tab = line.find('\t', first_tab + 1);
    if (second_tab == std::string::npos) {
        return false;
    }

    type = line[0];
    try {
        size = static_cast<std::uintmax_t>(std::stoull(line.substr(first_tab + 1, second_tab - first_tab - 1)));
    } catch (...) {
        size = 0;
    }
    name = line.substr(second_tab + 1);
    return !name.empty();
}

bool CommandAvailable(const std::string& executable) {
#ifdef _WIN32
    const std::string command = "where " + executable + " > NUL 2> NUL";
#else
    const std::string command = "command -v " + executable + " >/dev/null 2>/dev/null";
#endif
    return std::system(command.c_str()) == 0;
}

bool ContainsBatchUnsafeCharacter(const std::string& value) {
    return value.find('\n') != std::string::npos || value.find('\r') != std::string::npos;
}

bool ValidateSftpBatchPath(const std::string& path, const std::string& label, std::string& error) {
    if (ContainsBatchUnsafeCharacter(path)) {
        error = label + " contains a newline. SFTP batch commands cannot safely handle that path.";
        return false;
    }
    return true;
}

} // namespace

bool RemoteSftpProvider::Connect(const RemoteConnectionConfig& config, std::string& error) {
    config_ = config;
    config_.remote_root = NormalizeRemoteDirectory(config_.remote_root);
    if (config_.type != RemoteConnectionType::Ssh &&
        config_.type != RemoteConnectionType::Sftp) {
        error = "Only SSH/SFTP connections are implemented in this version.";
        return false;
    }
    if (Target().empty()) {
        error = "SFTP connection needs ssh_config_host or user/host.";
        return false;
    }
    if (!CommandAvailable("ssh")) {
        error = "Cannot find external ssh executable. Install OpenSSH client or add ssh to PATH.";
        return false;
    }
    if (!CommandAvailable("sftp")) {
        error = "Cannot find external sftp executable. Install OpenSSH client or add sftp to PATH.";
        return false;
    }
    if (!config_.password.empty() && !CommandAvailable("sshpass")) {
        error = "SFTP password authentication needs sshpass. Install sshpass, or use an SSH key / ssh-agent / ~/.ssh/config.";
        return false;
    }
    error.clear();
    return true;
}

bool RemoteSftpProvider::TestConnection(std::string& output, std::string& error) {
    std::vector<std::string> args = BuildSshArgs();
    args.push_back(Target());
    args.push_back("pwd");
    const RemoteCommandResult result = runner_.Run(args);
    output = result.output;
    if (result.exit_code != 0) {
        error = result.error.empty() ? "SSH connection test failed." : result.error;
        if (!result.output.empty()) {
            error += "\n" + result.output;
        }
        return false;
    }
    error.clear();
    if (output.empty()) {
        output = "SSH connection works.";
    }
    return true;
}

bool RemoteSftpProvider::List(
    const std::string& path,
    std::vector<RemoteEntry>& entries,
    std::string& error) {
    entries.clear();
    const std::string directory = NormalizeRemoteDirectory(path.empty() ? config_.remote_root : path);

    std::vector<std::string> args = BuildSshArgs();
    args.push_back(Target());
    args.push_back(
        "cd " + RemoteCommandRunner::ShellQuote(directory) +
        " && find . -mindepth 1 -maxdepth 1 -printf '%y\\t%s\\t%f\\n'");

    const RemoteCommandResult result = runner_.Run(args);
    if (result.exit_code != 0) {
        error = result.error.empty() ? "Cannot list remote directory." : result.error;
        if (!result.output.empty()) {
            error += "\n" + result.output;
        }
        return false;
    }

    std::istringstream lines(result.output);
    std::string line;
    while (std::getline(lines, line)) {
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        char type = '?';
        std::uintmax_t size = 0;
        std::string name;
        if (!SplitFindLine(line, type, size, name)) {
            continue;
        }

        RemoteEntry entry;
        entry.name = name;
        entry.path = JoinRemotePath(directory, name);
        entry.size = size;
        entry.hidden = !name.empty() && name.front() == '.';
        switch (type) {
            case 'd':
                entry.type = RemoteEntryType::Directory;
                break;
            case 'f':
                entry.type = RemoteEntryType::File;
                break;
            case 'l':
                entry.type = RemoteEntryType::Symlink;
                break;
            default:
                entry.type = RemoteEntryType::Other;
                break;
        }
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), [](const RemoteEntry& left, const RemoteEntry& right) {
        if (left.type == RemoteEntryType::Directory && right.type != RemoteEntryType::Directory) {
            return true;
        }
        if (left.type != RemoteEntryType::Directory && right.type == RemoteEntryType::Directory) {
            return false;
        }
        return left.name < right.name;
    });

    error.clear();
    return true;
}

bool RemoteSftpProvider::Download(
    const std::string& remote_path,
    const std::string& local_path,
    std::string& error) {
    if (!ValidateSftpBatchPath(remote_path, "Remote path", error) ||
        !ValidateSftpBatchPath(local_path, "Local path", error)) {
        return false;
    }
    std::string output;
    return RunSftpBatch(
        "get " + SftpQuote(remote_path) + " " + SftpQuote(local_path) + "\n",
        output,
        error);
}

bool RemoteSftpProvider::Upload(
    const std::string& local_path,
    const std::string& remote_path,
    std::string& error) {
    if (!ValidateSftpBatchPath(local_path, "Local path", error) ||
        !ValidateSftpBatchPath(remote_path, "Remote path", error)) {
        return false;
    }
    std::string output;
    return RunSftpBatch(
        "put " + SftpQuote(local_path) + " " + SftpQuote(remote_path) + "\n",
        output,
        error);
}

bool RemoteSftpProvider::DownloadDirectory(
    const std::string& remote_path,
    const std::string& local_path,
    std::string& error) {
    if (!ValidateSftpBatchPath(remote_path, "Remote path", error) ||
        !ValidateSftpBatchPath(local_path, "Local path", error)) {
        return false;
    }
    std::string output;
    return RunSftpBatch(
        "get -r " + SftpQuote(remote_path) + " " + SftpQuote(local_path) + "\n",
        output,
        error);
}

bool RemoteSftpProvider::UploadDirectory(
    const std::string& local_path,
    const std::string& remote_path,
    std::string& error) {
    if (!ValidateSftpBatchPath(local_path, "Local path", error) ||
        !ValidateSftpBatchPath(remote_path, "Remote path", error)) {
        return false;
    }
    std::string output;
    return RunSftpBatch(
        "put -r " + SftpQuote(local_path) + " " + SftpQuote(remote_path) + "\n",
        output,
        error);
}

bool RemoteSftpProvider::Rename(
    const std::string& old_path,
    const std::string& new_path,
    std::string& error) {
    if (!ValidateSftpBatchPath(old_path, "Old remote path", error) ||
        !ValidateSftpBatchPath(new_path, "New remote path", error)) {
        return false;
    }
    std::string output;
    return RunSftpBatch(
        "rename " + SftpQuote(old_path) + " " + SftpQuote(new_path) + "\n",
        output,
        error);
}

bool RemoteSftpProvider::RemoveFile(const std::string& path, std::string& error) {
    if (!ValidateSftpBatchPath(path, "Remote path", error)) {
        return false;
    }
    std::string output;
    return RunSftpBatch("rm " + SftpQuote(path) + "\n", output, error);
}

bool RemoteSftpProvider::MakeDirectory(const std::string& path, std::string& error) {
    if (!ValidateSftpBatchPath(path, "Remote path", error)) {
        return false;
    }
    std::string output;
    return RunSftpBatch("mkdir " + SftpQuote(path) + "\n", output, error);
}

bool RemoteSftpProvider::RemoveDirectory(const std::string& path, std::string& error) {
    if (!ValidateSftpBatchPath(path, "Remote path", error)) {
        return false;
    }
    std::string output;
    return RunSftpBatch("rmdir " + SftpQuote(path) + "\n", output, error);
}

std::vector<std::string> RemoteSftpProvider::BuildSshArgs() const {
    std::vector<std::string> args;
    if (!config_.password.empty()) {
        args.push_back("sshpass");
        args.push_back("-p");
        args.push_back(config_.password);
    }
    args.push_back("ssh");
    args.push_back("-o");
    args.push_back(config_.password.empty() ? "BatchMode=yes" : "BatchMode=no");
    args.push_back("-o");
    args.push_back("ConnectTimeout=10");
    if (!config_.known_hosts_file.empty()) {
        args.push_back("-o");
        args.push_back("UserKnownHostsFile=" + config_.known_hosts_file);
    }
    if (!config_.identity_file.empty()) {
        args.push_back("-i");
        args.push_back(config_.identity_file);
    }
    if (config_.port > 0 && config_.port != 22) {
        args.push_back("-p");
        args.push_back(std::to_string(config_.port));
    }
    return args;
}

std::vector<std::string> RemoteSftpProvider::BuildSftpArgs() const {
    std::vector<std::string> args;
    if (!config_.password.empty()) {
        args.push_back("sshpass");
        args.push_back("-p");
        args.push_back(config_.password);
    }
    args.push_back("sftp");
    args.push_back("-o");
    args.push_back(config_.password.empty() ? "BatchMode=yes" : "BatchMode=no");
    args.push_back("-o");
    args.push_back("ConnectTimeout=10");
    args.push_back("-b");
    args.push_back("-");
    if (!config_.known_hosts_file.empty()) {
        args.push_back("-o");
        args.push_back("UserKnownHostsFile=" + config_.known_hosts_file);
    }
    if (!config_.identity_file.empty()) {
        args.push_back("-i");
        args.push_back(config_.identity_file);
    }
    if (config_.port > 0 && config_.port != 22) {
        args.push_back("-P");
        args.push_back(std::to_string(config_.port));
    }
    args.push_back(Target());
    return args;
}

std::string RemoteSftpProvider::Target() const {
    if (!config_.ssh_config_host.empty()) {
        return config_.ssh_config_host;
    }
    if (config_.host.empty()) {
        return {};
    }
    if (config_.user.empty()) {
        return config_.host;
    }
    return config_.user + "@" + config_.host;
}

bool RemoteSftpProvider::RunSftpBatch(
    const std::string& batch,
    std::string& output,
    std::string& error) const {
    const RemoteCommandResult result = runner_.Run(BuildSftpArgs(), batch);
    output = result.output;
    if (result.exit_code != 0) {
        error = result.error.empty() ? "SFTP command failed." : result.error;
        if (!result.output.empty()) {
            error += "\n" + result.output;
        }
        return false;
    }
    error.clear();
    return true;
}

std::string RemoteSftpProvider::SftpQuote(const std::string& value) {
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            quoted += '\\';
        }
        quoted += ch;
    }
    quoted += "\"";
    return quoted;
}

} // namespace textlt
