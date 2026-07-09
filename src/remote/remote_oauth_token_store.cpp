#include "remote/remote_oauth_token_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "json_utils.hpp"

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

std::string SlugPart(std::string value) {
    value = Trim(std::move(value));
    std::string result;
    result.reserve(value.size());
    bool last_dash = false;
    for (unsigned char raw_ch : value) {
        const char ch = static_cast<char>(raw_ch);
        if (std::isalnum(raw_ch)) {
            result += static_cast<char>(std::tolower(raw_ch));
            last_dash = false;
            continue;
        }
        if (ch == '.' || ch == '_' || ch == '-') {
            result += ch;
            last_dash = false;
            continue;
        }
        if (!last_dash) {
            result += '-';
            last_dash = true;
        }
    }
    while (!result.empty() && result.front() == '-') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    return result.empty() ? "default" : result;
}

std::string ProviderSlug(RemoteConnectionType type) {
    switch (type) {
        case RemoteConnectionType::GoogleDrive:
            return "google-drive";
        case RemoteConnectionType::MicrosoftDrive:
            return "microsoft-drive";
        case RemoteConnectionType::Dropbox:
            return "dropbox";
        case RemoteConnectionType::Sftp:
            return "sftp";
    }
    return "remote";
}

RemoteOAuthToken ParseToken(const Json& object) {
    RemoteOAuthToken token;
    token.provider = JsonString(object, "provider");
    token.display_name = JsonString(object, "display_name");
    token.access_token = JsonString(object, "access_token");
    token.refresh_token = JsonString(object, "refresh_token");
    token.token_type = JsonString(object, "token_type", "Bearer");
    token.scope = JsonString(object, "scope");
    token.raw_response = JsonString(object, "raw_response");
    const auto expires_iter = object.find("expires_at_unix");
    if (expires_iter != object.end() && expires_iter->is_number_integer()) {
        token.expires_at_unix = expires_iter->get<std::int64_t>();
    }
    return token;
}

Json SerializeToken(const RemoteOAuthToken& token) {
    Json object = Json::object();
    object["provider"] = token.provider;
    object["display_name"] = token.display_name;
    object["access_token"] = token.access_token;
    object["refresh_token"] = token.refresh_token;
    object["token_type"] = token.token_type.empty() ? "Bearer" : token.token_type;
    object["scope"] = token.scope;
    object["expires_at_unix"] = token.expires_at_unix;
    object["raw_response"] = token.raw_response;
    return object;
}

} // namespace

bool RemoteOAuthToken::HasUsableToken() const {
    return !access_token.empty() || !refresh_token.empty();
}

bool IsCloudRemoteConnectionType(RemoteConnectionType type) {
    return type == RemoteConnectionType::GoogleDrive ||
        type == RemoteConnectionType::MicrosoftDrive ||
        type == RemoteConnectionType::Dropbox;
}

std::string RemoteTokenProviderName(RemoteConnectionType type) {
    switch (type) {
        case RemoteConnectionType::GoogleDrive:
            return "Google Drive";
        case RemoteConnectionType::MicrosoftDrive:
            return "Microsoft OneDrive / SharePoint";
        case RemoteConnectionType::Dropbox:
            return "Dropbox";
        case RemoteConnectionType::Sftp:
            return "SFTP / SSH";
    }
    return "Remote";
}

std::filesystem::path ExpandRemoteUserPath(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    if (path == "~" || path.rfind("~/", 0) == 0 || path.rfind("~\\", 0) == 0) {
#ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif
        if (home && std::string(home).size() > 0) {
            if (path.size() == 1) {
                return std::filesystem::path(home);
            }
            return std::filesystem::path(home) / path.substr(2);
        }
    }
    return std::filesystem::path(path);
}

std::filesystem::path DefaultRemoteTokenDirectory() {
#ifdef _WIN32
    const char* app_data = std::getenv("APPDATA");
    if (app_data && std::string(app_data).size() > 0) {
        return std::filesystem::path(app_data) / "textlt" / "remote_tokens";
    }
    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && std::string(user_profile).size() > 0) {
        return std::filesystem::path(user_profile) / "AppData" / "Roaming" / "textlt" / "remote_tokens";
    }
    return std::filesystem::path("remote_tokens");
#else
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return std::filesystem::path("remote_tokens");
    }
    return std::filesystem::path(home) / ".config" / "textlt" / "remote_tokens";
#endif
}

std::filesystem::path DefaultRemoteTokenPath(RemoteConnectionType type, const std::string& stable_name) {
    return DefaultRemoteTokenDirectory() / (ProviderSlug(type) + "-" + SlugPart(stable_name) + ".json");
}

RemoteOAuthTokenStore::RemoteOAuthTokenStore(std::filesystem::path token_path)
    : path_(std::move(token_path)) {
}

bool RemoteOAuthTokenStore::Exists() const {
    std::error_code error;
    return !path_.empty() && std::filesystem::is_regular_file(path_, error);
}

bool RemoteOAuthTokenStore::EnsureParentDirectory(std::string& error) const {
    error.clear();
    if (path_.empty()) {
        error = "Token file path is empty.";
        return false;
    }
    std::error_code fs_error;
    const std::filesystem::path parent = path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, fs_error);
        if (fs_error) {
            error = "Cannot create token directory: " + parent.string() + "\n" + fs_error.message();
            return false;
        }
    }
    return true;
}

bool RemoteOAuthTokenStore::Load(RemoteOAuthToken& token, std::string& error) const {
    error.clear();
    token = RemoteOAuthToken{};
    if (path_.empty()) {
        error = "Token file path is empty.";
        return false;
    }
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        error = "Token file does not exist: " + path_.string();
        return false;
    }
    Json parsed = Json::parse(file, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error = "Token file is not valid JSON: " + path_.string();
        return false;
    }
    token = ParseToken(parsed);
    return true;
}

bool RemoteOAuthTokenStore::Save(const RemoteOAuthToken& token, std::string& error) const {
    if (!EnsureParentDirectory(error)) {
        return false;
    }
    if (!WriteJsonAtomically(path_, SerializeToken(token))) {
        error = "Cannot write token file: " + path_.string();
        return false;
    }
    error.clear();
    return true;
}

bool RemoteOAuthTokenStore::Remove(std::string& error) const {
    error.clear();
    if (path_.empty()) {
        error = "Token file path is empty.";
        return false;
    }
    std::error_code fs_error;
    std::filesystem::remove(path_, fs_error);
    if (fs_error) {
        error = "Cannot remove token file: " + path_.string() + "\n" + fs_error.message();
        return false;
    }
    return true;
}

std::string DescribeRemoteOAuthTokenStatus(const RemoteConnectionConfig& config) {
    if (!IsCloudRemoteConnectionType(config.type)) {
        return "This connection type does not use OAuth token files.";
    }
    const std::filesystem::path path = ExpandRemoteUserPath(config.token_file);
    if (path.empty()) {
        return "Token file is not configured.";
    }
    RemoteOAuthTokenStore store(path);
    if (!store.Exists()) {
        return "Token file is configured but not created yet: " + path.string();
    }
    RemoteOAuthToken token;
    std::string error;
    if (!store.Load(token, error)) {
        return error;
    }
    std::ostringstream summary;
    summary << "Token file exists: " << path.string();
    if (!token.provider.empty()) {
        summary << "\nProvider: " << token.provider;
    }
    if (!token.display_name.empty()) {
        summary << "\nName: " << token.display_name;
    }
    summary << "\nToken state: " << (token.HasUsableToken() ? "has access/refresh token" : "placeholder only");
    if (token.expires_at_unix > 0) {
        summary << "\nExpires at unix: " << token.expires_at_unix;
    }
    return summary.str();
}

} // namespace textlt
