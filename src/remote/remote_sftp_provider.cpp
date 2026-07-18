#include "remote/remote_sftp_provider.hpp"

#include "remote/remote_ssh_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

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

bool CommandAvailable(const std::string& executable) {
#ifdef _WIN32
    return std::system(("where " + executable + " > NUL 2> NUL").c_str()) == 0;
#else
    return std::system(("command -v " + executable + " >/dev/null 2>/dev/null").c_str()) == 0;
#endif
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

bool ParseLongListLine(const std::string& line, RemoteEntry& entry) {
    if (line.size() < 10 || (line.front() != 'd' && line.front() != '-' && line.front() != 'l')) {
        return false;
    }
    std::istringstream stream(line);
    std::string permissions;
    std::string links;
    std::string owner;
    std::string group;
    std::string size;
    std::string month;
    std::string day;
    std::string time_or_year;
    if (!(stream >> permissions >> links >> owner >> group >> size >> month >> day >> time_or_year)) {
        return false;
    }
    std::string name;
    std::getline(stream, name);
    name = Trim(name);
    if (permissions.front() == 'l') {
        const size_t arrow = name.find(" -> ");
        if (arrow != std::string::npos) {
            name.erase(arrow);
        }
    }
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    entry.name = name;
    entry.type = permissions.front() == 'd'
        ? RemoteEntryType::Directory
        : permissions.front() == 'l' ? RemoteEntryType::Symlink : RemoteEntryType::File;
    try {
        entry.size = static_cast<std::uintmax_t>(std::stoull(size));
    } catch (...) {
        entry.size = 0;
    }
    return true;
}

} // namespace

bool RemoteSftpProvider::Connect(const RemoteConnectionConfig& config, std::string& error) {
    config_ = config;
    config_.remote_root = NormalizeRemoteDirectory(config_.remote_root);
    if (config_.type != RemoteConnectionType::Sftp) {
        error = "SFTP provider needs an SFTP connection.";
        return false;
    }
    if (config_.ssh_config_host.empty() || ContainsBatchUnsafeCharacter(config_.ssh_config_host)) {
        error = "SFTP connection needs a valid Host alias from ~/.ssh/config.";
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

    const RemoteCommandResult resolved = runner_.Run({
        "ssh", "-G", "-F", DefaultSshConfigPath().string(), config_.ssh_config_host,
    });
    if (resolved.exit_code != 0) {
        error = resolved.error.empty()
            ? "OpenSSH could not resolve the selected SSH config Host alias."
            : Trim(resolved.error);
        return false;
    }
    error.clear();
    return true;
}

bool RemoteSftpProvider::TestConnection(std::string& output, std::string& error) {
    if (!RunSftpBatch("pwd\n", output, error)) {
        return false;
    }
    if (output.empty()) {
        output = "SFTP connection works.";
    }
    return true;
}

bool RemoteSftpProvider::List(
    const std::string& path,
    std::vector<RemoteEntry>& entries,
    std::string& error) {
    const std::string directory = NormalizeRemoteDirectory(path.empty() ? config_.remote_root : path);
    std::string output;
    if (!RunSftpBatch("ls -la " + SftpQuote(directory) + "\n", output, error)) {
        entries.clear();
        return false;
    }
    ParseSftpListing(output, directory, entries);
    std::sort(entries.begin(), entries.end(), [](const RemoteEntry& left, const RemoteEntry& right) {
        if (left.type == RemoteEntryType::Directory && right.type != RemoteEntryType::Directory) return true;
        if (left.type != RemoteEntryType::Directory && right.type == RemoteEntryType::Directory) return false;
        return left.name < right.name;
    });
    error.clear();
    return true;
}

void RemoteSftpProvider::ParseSftpListing(
    const std::string& output,
    const std::string& directory,
    std::vector<RemoteEntry>& entries) {
    entries.clear();
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        line = Trim(line);
        if (line.empty() || line.rfind("sftp>", 0) == 0) {
            continue;
        }
        RemoteEntry entry;
        if (!ParseLongListLine(line, entry)) {
            continue;
        }
        entry.path = JoinRemotePath(directory, entry.name);
        entry.hidden = !entry.name.empty() && entry.name.front() == '.';
        entries.push_back(std::move(entry));
    }
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
    if (!ValidateSftpBatchPath(path, "Remote path", error)) return false;
    std::string output;
    return RunSftpBatch("rm " + SftpQuote(path) + "\n", output, error);
}

bool RemoteSftpProvider::MakeDirectory(const std::string& path, std::string& error) {
    if (!ValidateSftpBatchPath(path, "Remote path", error)) return false;
    std::string output;
    return RunSftpBatch("mkdir " + SftpQuote(path) + "\n", output, error);
}

bool RemoteSftpProvider::RemoveDirectory(const std::string& path, std::string& error) {
    if (!ValidateSftpBatchPath(path, "Remote path", error)) return false;
    std::string output;
    return RunSftpBatch("rmdir " + SftpQuote(path) + "\n", output, error);
}

std::vector<std::string> RemoteSftpProvider::BuildSftpArgs() const {
    return {
        "sftp",
        "-F", DefaultSshConfigPath().string(),
        "-o", "BatchMode=yes",
        "-o", "ConnectTimeout=10",
        "-b", "-",
        config_.ssh_config_host,
    };
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
    quoted += '"';
    return quoted;
}

} // namespace textlt
