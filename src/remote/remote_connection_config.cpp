#include "remote/remote_connection_config.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <vector>

namespace textlt {

std::string RemoteConnectionTypeToString(RemoteConnectionType type) {
    switch (type) {
        case RemoteConnectionType::Sftp:
            return "sftp";
        case RemoteConnectionType::GoogleDrive:
            return "google_drive";
        case RemoteConnectionType::MicrosoftDrive:
            return "microsoft_drive";
        case RemoteConnectionType::Dropbox:
            return "dropbox";
    }
    return "sftp";
}

RemoteConnectionType RemoteConnectionTypeFromString(const std::string& value) {
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "google" || normalized == "google_drive" || normalized == "google-drive") {
        return RemoteConnectionType::GoogleDrive;
    }
    if (normalized == "microsoft" || normalized == "microsoft_drive" ||
        normalized == "microsoft-drive" || normalized == "onedrive" || normalized == "one_drive") {
        return RemoteConnectionType::MicrosoftDrive;
    }
    if (normalized == "dropbox") {
        return RemoteConnectionType::Dropbox;
    }
    return RemoteConnectionType::Sftp;
}

std::string MakeRemoteConnectionId() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "remote-" + std::to_string(millis);
}

std::string NormalizeRemoteDirectory(std::string path) {
    if (path.empty()) {
        return "/";
    }

    std::replace(path.begin(), path.end(), '\\', '/');
    std::vector<std::string> parts;
    std::stringstream stream(path);
    std::string part;
    const bool absolute = !path.empty() && path.front() == '/';

    while (std::getline(stream, part, '/')) {
        if (part.empty() || part == ".") {
            continue;
        }
        if (part == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
            continue;
        }
        parts.push_back(part);
    }

    std::string result = absolute ? "/" : "";
    for (size_t index = 0; index < parts.size(); ++index) {
        if (index > 0 || absolute) {
            if (!result.empty() && result.back() != '/') {
                result += '/';
            }
        }
        result += parts[index];
    }

    if (result.empty()) {
        return absolute ? "/" : ".";
    }
    return result;
}

std::string JoinRemotePath(const std::string& directory, const std::string& name) {
    if (name.empty()) {
        return NormalizeRemoteDirectory(directory);
    }
    if (!name.empty() && name.front() == '/') {
        return NormalizeRemoteDirectory(name);
    }
    std::string base = directory.empty() ? "/" : directory;
    if (base.back() != '/') {
        base += '/';
    }
    return NormalizeRemoteDirectory(base + name);
}

std::string RemoteParentPath(const std::string& path) {
    const std::string normalized = NormalizeRemoteDirectory(path);
    if (normalized.empty() || normalized == "/") {
        return "/";
    }
    const size_t slash = normalized.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return normalized.substr(0, slash);
}

std::string RemoteBaseName(const std::string& path) {
    const std::string normalized = NormalizeRemoteDirectory(path);
    if (normalized == "/") {
        return "/";
    }
    const size_t slash = normalized.find_last_of('/');
    if (slash == std::string::npos) {
        return normalized;
    }
    return normalized.substr(slash + 1);
}

} // namespace textlt
