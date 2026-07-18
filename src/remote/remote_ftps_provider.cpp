#include "remote/remote_ftps_provider.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <system_error>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

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

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ContainsControlNewline(const std::string& value) {
    return value.find('\n') != std::string::npos || value.find('\r') != std::string::npos;
}

std::string CurlConfigQuote(const std::string& value) {
    std::string quoted = "\"";
    for (char ch : value) {
        switch (ch) {
            case '\\': quoted += "\\\\"; break;
            case '"': quoted += "\\\""; break;
            case '\n': quoted += "\\n"; break;
            case '\r': break;
            case '\t': quoted += "\\t"; break;
            default: quoted += ch; break;
        }
    }
    quoted += '"';
    return quoted;
}

std::string UrlEncodePath(const std::string& path) {
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex;
    for (unsigned char ch : path) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/') {
            encoded << static_cast<char>(ch);
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return encoded.str();
}

std::filesystem::path MakeConfigPath() {
    static std::atomic<unsigned long long> sequence{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("textlt-ftps-" + std::to_string(ticks) + "-" +
         std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + ".conf");
}

bool WritePrivateFile(const std::filesystem::path& path, const std::string& text) {
#ifdef _WIN32
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    return static_cast<bool>(file);
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        return false;
    }
    size_t written = 0;
    while (written < text.size()) {
        const ssize_t count = ::write(fd, text.data() + written, text.size() - written);
        if (count <= 0) {
            ::close(fd);
            std::error_code remove_error;
            std::filesystem::remove(path, remove_error);
            return false;
        }
        written += static_cast<size_t>(count);
    }
    return ::close(fd) == 0;
#endif
}

bool CommandAvailable(const std::string& executable) {
#ifdef _WIN32
    return std::system(("where " + executable + " > NUL 2> NUL").c_str()) == 0;
#else
    return std::system(("command -v " + executable + " >/dev/null 2>/dev/null").c_str()) == 0;
#endif
}

bool ParseUnixListLine(const std::string& line, RemoteEntry& entry) {
    if (line.size() < 10 || (line[0] != 'd' && line[0] != '-' && line[0] != 'l')) {
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
    if (permissions[0] == 'l') {
        const size_t arrow = name.find(" -> ");
        if (arrow != std::string::npos) {
            name.erase(arrow);
        }
    }
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    entry.name = name;
    entry.type = permissions[0] == 'd'
        ? RemoteEntryType::Directory
        : permissions[0] == 'l' ? RemoteEntryType::Symlink : RemoteEntryType::File;
    try {
        entry.size = static_cast<std::uintmax_t>(std::stoull(size));
    } catch (...) {
        entry.size = 0;
    }
    return true;
}

bool ParseWindowsListLine(const std::string& line, RemoteEntry& entry) {
    static const std::regex pattern(
        R"(^\s*\d{2}-\d{2}-\d{2,4}\s+\d{2}:\d{2}(?:AM|PM)\s+(<DIR>|\d+)\s+(.+?)\s*$)",
        std::regex::icase);
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) {
        return false;
    }
    entry.name = match[2].str();
    if (entry.name == "." || entry.name == "..") {
        return false;
    }
    if (Lower(match[1].str()) == "<dir>") {
        entry.type = RemoteEntryType::Directory;
    } else {
        entry.type = RemoteEntryType::File;
        try {
            entry.size = static_cast<std::uintmax_t>(std::stoull(match[1].str()));
        } catch (...) {
            entry.size = 0;
        }
    }
    return true;
}

} // namespace

bool RemoteFtpsProvider::Connect(const RemoteConnectionConfig& config, std::string& error) {
    config_ = config;
    config_.remote_root = NormalizeRemoteDirectory(config_.remote_root);
    config_.ftps_tls_mode = Lower(config_.ftps_tls_mode.empty() ? "explicit" : config_.ftps_tls_mode);
    if (config_.type != RemoteConnectionType::Ftps) {
        error = "FTPS provider needs an FTPS connection.";
        return false;
    }
    if (config_.host.empty() || ContainsControlNewline(config_.host)) {
        error = "FTPS connection needs a valid Host.";
        return false;
    }
    if (config_.ftps_tls_mode != "explicit" && config_.ftps_tls_mode != "implicit") {
        error = "FTPS TLS mode must be explicit or implicit.";
        return false;
    }
    if (!CommandAvailable("curl")) {
        error = "Cannot find external curl executable. Install curl or add it to PATH.";
        return false;
    }
    error.clear();
    return true;
}

bool RemoteFtpsProvider::TestConnection(std::string& output, std::string& error) {
    std::vector<RemoteEntry> entries;
    if (!List(config_.remote_root, entries, error)) {
        output.clear();
        return false;
    }
    output = "FTPS connection works. Listed " + std::to_string(entries.size()) + " item(s).";
    return true;
}

bool RemoteFtpsProvider::ParseDirectoryListing(
    const std::string& listing,
    const std::string& directory,
    std::vector<RemoteEntry>& entries) {
    entries.clear();
    std::istringstream lines(listing);
    std::string line;
    while (std::getline(lines, line)) {
        line = Trim(line);
        if (line.empty() || line.rfind("total ", 0) == 0) {
            continue;
        }
        RemoteEntry entry;
        if (!ParseUnixListLine(line, entry) && !ParseWindowsListLine(line, entry)) {
            continue;
        }
        entry.path = JoinRemotePath(directory, entry.name);
        entry.hidden = !entry.name.empty() && entry.name.front() == '.';
        entries.push_back(std::move(entry));
    }
    return !entries.empty() || Trim(listing).empty();
}

bool RemoteFtpsProvider::List(
    const std::string& path,
    std::vector<RemoteEntry>& entries,
    std::string& error) {
    std::string output;
    if (!RunCurl(BuildBaseConfig(path, true), output, error)) {
        entries.clear();
        return false;
    }
    if (!ParseDirectoryListing(output, NormalizeRemoteDirectory(path), entries)) {
        error = "FTPS server returned an unsupported directory listing format.";
        return false;
    }
    std::sort(entries.begin(), entries.end(), [](const RemoteEntry& left, const RemoteEntry& right) {
        if (left.type == RemoteEntryType::Directory && right.type != RemoteEntryType::Directory) return true;
        if (left.type != RemoteEntryType::Directory && right.type == RemoteEntryType::Directory) return false;
        return left.name < right.name;
    });
    error.clear();
    return true;
}

bool RemoteFtpsProvider::Download(
    const std::string& remote_path,
    const std::string& local_path,
    std::string& error) {
    std::string output;
    std::string config = BuildBaseConfig(remote_path, false);
    config += "output = " + CurlConfigQuote(local_path) + "\n";
    return RunCurl(config, output, error);
}

bool RemoteFtpsProvider::Upload(
    const std::string& local_path,
    const std::string& remote_path,
    std::string& error) {
    std::string output;
    std::string config = BuildBaseConfig(remote_path, false);
    config += "upload-file = " + CurlConfigQuote(local_path) + "\n";
    return RunCurl(config, output, error);
}

bool RemoteFtpsProvider::DownloadDirectory(
    const std::string& remote_path,
    const std::string& local_path,
    std::string& error) {
    return DownloadDirectoryRecursive(remote_path, std::filesystem::path(local_path), error);
}

bool RemoteFtpsProvider::UploadDirectory(
    const std::string& local_path,
    const std::string& remote_path,
    std::string& error) {
    return UploadDirectoryRecursive(std::filesystem::path(local_path), remote_path, error);
}

bool RemoteFtpsProvider::Rename(
    const std::string& old_path,
    const std::string& new_path,
    std::string& error) {
    return RunQuote({"RNFR " + old_path, "RNTO " + new_path}, RemoteParentPath(old_path), error);
}

bool RemoteFtpsProvider::RemoveFile(const std::string& path, std::string& error) {
    return RunQuote({"DELE " + path}, RemoteParentPath(path), error);
}

bool RemoteFtpsProvider::MakeDirectory(const std::string& path, std::string& error) {
    return RunQuote({"MKD " + path}, RemoteParentPath(path), error);
}

bool RemoteFtpsProvider::RemoveDirectory(const std::string& path, std::string& error) {
    return RunQuote({"RMD " + path}, RemoteParentPath(path), error);
}

std::string RemoteFtpsProvider::BuildUrl(const std::string& path, bool directory) const {
    const bool implicit = config_.ftps_tls_mode == "implicit";
    std::string normalized = NormalizeRemoteDirectory(path.empty() ? "/" : path);
    if (normalized.empty() || normalized.front() != '/') {
        normalized.insert(normalized.begin(), '/');
    }
    if (directory && normalized.back() != '/') {
        normalized += '/';
    }
    const int default_port = implicit ? 990 : 21;
    const std::string port = config_.port > 0 && config_.port != default_port
        ? ":" + std::to_string(config_.port)
        : std::string{};
    return std::string(implicit ? "ftps://" : "ftp://") + config_.host + port +
        UrlEncodePath(normalized);
}

std::string RemoteFtpsProvider::BuildBaseConfig(const std::string& path, bool directory) const {
    std::string config;
    config += "silent\nshow-error\nfail\nconnect-timeout = 15\nmax-time = 120\n";
    config += config_.ftps_passive ? "ftp-pasv\n" : "ftp-port = \"-\"\n";
    if (config_.ftps_tls_mode == "explicit") {
        config += "ssl-reqd\n";
    }
    config += "insecure\n";
    config += "user = " + CurlConfigQuote(config_.user + ":" + config_.password) + "\n";
    config += "url = " + CurlConfigQuote(BuildUrl(path, directory)) + "\n";
    return config;
}

bool RemoteFtpsProvider::RunCurl(
    const std::string& config_text,
    std::string& output,
    std::string& error) const {
    const std::filesystem::path config_path = MakeConfigPath();
    if (!WritePrivateFile(config_path, config_text)) {
        error = "Cannot create protected temporary FTPS configuration.";
        return false;
    }
    const RemoteCommandResult result = runner_.Run({"curl", "--config", config_path.string()});
    std::error_code remove_error;
    std::filesystem::remove(config_path, remove_error);
    output = result.output;
    if (result.exit_code != 0) {
        error = result.error.empty() ? "FTPS curl command failed." : Trim(result.error);
        return false;
    }
    error.clear();
    return true;
}

bool RemoteFtpsProvider::RunQuote(
    const std::vector<std::string>& commands,
    const std::string& directory,
    std::string& error) const {
    std::string config = BuildBaseConfig(directory, true);
    for (const std::string& command : commands) {
        if (ContainsControlNewline(command)) {
            error = "FTPS command path contains a newline.";
            return false;
        }
        config += "quote = " + CurlConfigQuote(command) + "\n";
    }
    std::string output;
    return RunCurl(config, output, error);
}

bool RemoteFtpsProvider::DownloadDirectoryRecursive(
    const std::string& remote_path,
    const std::filesystem::path& local_path,
    std::string& error) {
    std::error_code fs_error;
    std::filesystem::create_directories(local_path, fs_error);
    if (fs_error) {
        error = "Cannot create local directory: " + local_path.string();
        return false;
    }
    std::vector<RemoteEntry> entries;
    if (!List(remote_path, entries, error)) {
        return false;
    }
    for (const RemoteEntry& entry : entries) {
        const std::filesystem::path destination = local_path / entry.name;
        if (entry.type == RemoteEntryType::Directory) {
            if (!DownloadDirectoryRecursive(entry.path, destination, error)) return false;
        } else if (entry.type == RemoteEntryType::File) {
            if (!Download(entry.path, destination.string(), error)) return false;
        }
    }
    return true;
}

bool RemoteFtpsProvider::UploadDirectoryRecursive(
    const std::filesystem::path& local_path,
    const std::string& remote_path,
    std::string& error) {
    if (!MakeDirectory(remote_path, error)) {
        return false;
    }
    std::error_code fs_error;
    std::filesystem::directory_iterator iter(local_path, fs_error);
    const std::filesystem::directory_iterator end;
    if (fs_error) {
        error = "Cannot read local directory: " + local_path.string();
        return false;
    }
    for (; iter != end; iter.increment(fs_error)) {
        if (fs_error) {
            error = "Cannot read local directory: " + local_path.string();
            return false;
        }
        const std::string destination = JoinRemotePath(remote_path, iter->path().filename().string());
        if (iter->is_directory()) {
            if (!UploadDirectoryRecursive(iter->path(), destination, error)) return false;
        } else if (iter->is_regular_file()) {
            if (!Upload(iter->path().string(), destination, error)) return false;
        }
    }
    return true;
}

} // namespace textlt
