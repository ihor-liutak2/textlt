#include "remote/remote_dropbox_provider.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <utility>


#include "json_utils.hpp"
#include "remote/remote_http_client.hpp"
#include "remote/remote_oauth_token_store.hpp"

namespace textlt {
namespace {

constexpr const char* kDropboxApiHost = "https://api.dropboxapi.com/2";
constexpr const char* kDropboxContentHost = "https://content.dropboxapi.com/2";
constexpr const char* kUserAgent = "textlt/1.0";

using HttpResult = RemoteHttpResponse;

std::string TrimForError(std::string value, size_t max_size = 800) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    if (value.size() > max_size) {
        value.resize(max_size);
        value += "...";
    }
    return value;
}

std::string BuildHttpError(const std::string& prefix, const HttpResult& result) {
    std::ostringstream message;
    message << prefix;
    if (result.status_code > 0) {
        message << " HTTP " << result.status_code;
    }
    if (!result.error.empty()) {
        message << "\n" << result.error;
    }
    const std::string body = TrimForError(result.body);
    if (!body.empty()) {
        message << "\n" << body;
    }
    return message.str();
}

std::string AuthorizationHeader(const std::string& token_type, const std::string& access_token) {
    return "Authorization: " + (token_type.empty() ? "Bearer" : token_type) + " " + access_token;
}

std::string DropboxScopeErrorHint(const std::string& body) {
    const Json parsed = Json::parse(body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return {};
    }
    const auto error_iter = parsed.find("error");
    if (error_iter == parsed.end() || !error_iter->is_object()) {
        return {};
    }
    const std::string tag = JsonString(*error_iter, ".tag");
    if (tag != "missing_scope") {
        return {};
    }
    const std::string required = JsonString(*error_iter, "required_scope");
    if (required.empty()) {
        return "Dropbox token is missing required permissions. Re-authorize with the required scopes in the Dropbox developer console.";
    }
    return "Dropbox token is missing scope: " + required + ". Add it in the Dropbox developer console and re-authorize.";
}

HttpResult PostJson(
    const std::string& url,
    const std::string& access_token,
    const std::string& token_type,
    const Json& request_body) {
    RemoteHttpClient client;
    return client.Request(
        "POST",
        url,
        {
            AuthorizationHeader(token_type, access_token),
            "Content-Type: application/json",
        },
        request_body.dump(),
        120);
}

HttpResult PostContentDownload(
    const std::string& url,
    const std::string& access_token,
    const std::string& token_type,
    const Json& api_arg,
    const std::filesystem::path& local_path) {
    RemoteHttpClient client;
    return client.Download(
        "POST",
        url,
        {
            AuthorizationHeader(token_type, access_token),
            "Dropbox-API-Arg: " + api_arg.dump(),
        },
        local_path,
        {},
        300);
}

HttpResult PostContentUpload(
    const std::string& url,
    const std::string& access_token,
    const std::string& token_type,
    const Json& api_arg,
    const std::filesystem::path& local_path) {
    RemoteHttpClient client;
    return client.UploadFile(
        "POST",
        url,
        {
            AuthorizationHeader(token_type, access_token),
            "Content-Type: application/octet-stream",
            "Dropbox-API-Arg: " + api_arg.dump(),
        },
        local_path,
        300);
}

RemoteEntryType DropboxEntryType(const Json& entry) {
    const std::string tag = JsonString(entry, ".tag");
    if (tag == "folder") {
        return RemoteEntryType::Directory;
    }
    if (tag == "file") {
        return RemoteEntryType::File;
    }
    return RemoteEntryType::Other;
}

std::uintmax_t DropboxEntrySize(const Json& entry) {
    const auto iter = entry.find("size");
    if (iter == entry.end()) {
        return 0;
    }
    if (iter->is_number_unsigned()) {
        return static_cast<std::uintmax_t>(iter->get<unsigned long long>());
    }
    if (iter->is_number_integer()) {
        const long long value = iter->get<long long>();
        return value > 0 ? static_cast<std::uintmax_t>(value) : 0;
    }
    return 0;
}

void SortRemoteEntries(std::vector<RemoteEntry>& entries) {
    std::sort(entries.begin(), entries.end(), [](const RemoteEntry& left, const RemoteEntry& right) {
        if (left.type == RemoteEntryType::Directory && right.type != RemoteEntryType::Directory) {
            return true;
        }
        if (left.type != RemoteEntryType::Directory && right.type == RemoteEntryType::Directory) {
            return false;
        }
        std::string left_name = left.name;
        std::string right_name = right.name;
        std::transform(left_name.begin(), left_name.end(), left_name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        std::transform(right_name.begin(), right_name.end(), right_name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return left_name < right_name;
    });
}

} // namespace

bool RemoteDropboxProvider::Connect(const RemoteConnectionConfig& config, std::string& error) {
    config_ = config;
    config_.remote_root = NormalizeRemoteDirectory(config_.remote_root.empty() ? "/" : config_.remote_root);
    access_token_.clear();
    token_type_ = "Bearer";

    if (config_.type != RemoteConnectionType::Dropbox) {
        error = "RemoteDropboxProvider can only use Dropbox connections.";
        return false;
    }

    const std::filesystem::path token_path = ExpandRemoteUserPath(config_.token_file);
    if (token_path.empty()) {
        error = "Dropbox connection needs a token file with an access_token.";
        return false;
    }

    RemoteOAuthToken token;
    RemoteOAuthTokenStore store(token_path);
    if (!store.Load(token, error)) {
        return false;
    }
    if (token.access_token.empty()) {
        error = "Dropbox token file is a placeholder. Add an access_token before using Dropbox files.";
        return false;
    }

    access_token_ = token.access_token;
    token_type_ = token.token_type.empty() ? "Bearer" : token.token_type;
    error.clear();
    return true;
}

bool RemoteDropboxProvider::TestConnection(std::string& output, std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    HttpResult result = PostJson(
        std::string(kDropboxApiHost) + "/users/get_current_account",
        access_token_,
        token_type_,
        Json(nullptr));
    if (!result.ok) {
        const std::string hint = DropboxScopeErrorHint(result.body);
        error = hint.empty() ? BuildHttpError("Dropbox connection test failed.", result) : hint;
        return false;
    }

    const Json parsed = Json::parse(result.body, nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object()) {
        const Json& name = parsed.contains("name") ? parsed["name"] : Json::object();
        const std::string display_name = name.is_object() ? JsonString(name, "display_name") : std::string{};
        const std::string email = JsonString(parsed, "email");
        output = "Dropbox connection works.";
        if (!display_name.empty()) {
            output += "\nName: " + display_name;
        }
        if (!email.empty()) {
            output += "\nEmail: " + email;
        }
    } else {
        output = "Dropbox connection works.";
    }
    error.clear();
    return true;
}

bool RemoteDropboxProvider::List(
    const std::string& path,
    std::vector<RemoteEntry>& entries,
    std::string& error) {
    entries.clear();
    if (!EnsureConnected(error)) {
        return false;
    }

    Json request = Json::object();
    request["path"] = DropboxApiPathFromRemotePath(path.empty() ? config_.remote_root : path);
    request["recursive"] = false;
    request["include_media_info"] = false;
    request["include_deleted"] = false;
    request["include_has_explicit_shared_members"] = false;
    request["include_mounted_folders"] = true;
    request["limit"] = 2000;

    Json parsed;
    for (;;) {
        HttpResult result = PostJson(
            std::string(kDropboxApiHost) + (request.contains("cursor") ? "/files/list_folder/continue" : "/files/list_folder"),
            access_token_,
            token_type_,
            request);
        if (!result.ok) {
            const std::string hint = DropboxScopeErrorHint(result.body);
            error = hint.empty() ? BuildHttpError("Cannot list Dropbox directory.", result) : hint;
            return false;
        }

        parsed = Json::parse(result.body, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            error = "Dropbox list_folder returned invalid JSON.";
            return false;
        }

        const auto entries_iter = parsed.find("entries");
        if (entries_iter != parsed.end() && entries_iter->is_array()) {
            for (const Json& object : *entries_iter) {
                if (!object.is_object()) {
                    continue;
                }
                RemoteEntry entry;
                entry.name = JsonString(object, "name");
                entry.path = RemotePathFromDropboxDisplayPath(JsonString(object, "path_display"));
                if (entry.path.empty() && !entry.name.empty()) {
                    entry.path = JoinRemotePath(path.empty() ? config_.remote_root : path, entry.name);
                }
                entry.type = DropboxEntryType(object);
                entry.size = DropboxEntrySize(object);
                entry.hidden = !entry.name.empty() && entry.name.front() == '.';
                if (!entry.name.empty() && entry.type != RemoteEntryType::Other) {
                    entries.push_back(std::move(entry));
                }
            }
        }

        const bool has_more = JsonBool(parsed, "has_more", false);
        if (!has_more) {
            break;
        }
        const std::string cursor = JsonString(parsed, "cursor");
        if (cursor.empty()) {
            break;
        }
        request = Json::object();
        request["cursor"] = cursor;
    }

    SortRemoteEntries(entries);
    error.clear();
    return true;
}

bool RemoteDropboxProvider::Download(
    const std::string& remote_path,
    const std::string& local_path,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }
    Json api_arg = Json::object();
    api_arg["path"] = DropboxApiPathFromRemotePath(remote_path);

    HttpResult result = PostContentDownload(
        std::string(kDropboxContentHost) + "/files/download",
        access_token_,
        token_type_,
        api_arg,
        std::filesystem::path(local_path));
    if (!result.ok) {
        const std::string hint = DropboxScopeErrorHint(result.body);
        error = hint.empty() ? BuildHttpError("Dropbox download failed.", result) : hint;
        return false;
    }
    error.clear();
    return true;
}

bool RemoteDropboxProvider::Upload(
    const std::string& local_path,
    const std::string& remote_path,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    std::error_code fs_error;
    if (!std::filesystem::is_regular_file(local_path, fs_error)) {
        error = fs_error ? fs_error.message() : "Local upload source is not a regular file.";
        return false;
    }

    Json api_arg = Json::object();
    api_arg["path"] = DropboxApiPathFromRemotePath(remote_path);
    api_arg["mode"] = "overwrite";
    api_arg["autorename"] = false;
    api_arg["mute"] = false;
    api_arg["strict_conflict"] = false;

    HttpResult result = PostContentUpload(
        std::string(kDropboxContentHost) + "/files/upload",
        access_token_,
        token_type_,
        api_arg,
        std::filesystem::path(local_path));
    if (!result.ok) {
        const std::string hint = DropboxScopeErrorHint(result.body);
        error = hint.empty() ? BuildHttpError("Dropbox upload failed.", result) : hint;
        return false;
    }
    error.clear();
    return true;
}

bool RemoteDropboxProvider::DownloadDirectory(
    const std::string& remote_path,
    const std::string& local_path,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    std::error_code fs_error;
    std::filesystem::create_directories(local_path, fs_error);
    if (fs_error) {
        error = "Cannot create local directory: " + local_path + "\n" + fs_error.message();
        return false;
    }

    std::vector<RemoteEntry> entries;
    if (!List(remote_path, entries, error)) {
        return false;
    }

    for (const RemoteEntry& entry : entries) {
        const std::filesystem::path target = std::filesystem::path(local_path) / entry.name;
        if (entry.type == RemoteEntryType::Directory) {
            if (!DownloadDirectory(entry.path, target.string(), error)) {
                return false;
            }
        } else if (entry.type == RemoteEntryType::File || entry.type == RemoteEntryType::Symlink) {
            if (!Download(entry.path, target.string(), error)) {
                return false;
            }
        }
    }
    error.clear();
    return true;
}

bool RemoteDropboxProvider::UploadDirectory(
    const std::string& local_path,
    const std::string& remote_path,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    std::error_code fs_error;
    if (!std::filesystem::is_directory(local_path, fs_error)) {
        error = fs_error ? fs_error.message() : "Local upload source is not a directory.";
        return false;
    }

    if (!MakeDirectoryIfNeeded(remote_path, error)) {
        return false;
    }

    for (const std::filesystem::directory_entry& child : std::filesystem::directory_iterator(local_path, fs_error)) {
        if (fs_error) {
            error = "Cannot read local directory: " + local_path + "\n" + fs_error.message();
            return false;
        }
        const std::string child_remote_path = JoinRemotePath(remote_path, child.path().filename().string());
        if (child.is_directory(fs_error)) {
            if (!UploadDirectory(child.path().string(), child_remote_path, error)) {
                return false;
            }
        } else if (child.is_regular_file(fs_error) || child.is_symlink(fs_error)) {
            if (!Upload(child.path().string(), child_remote_path, error)) {
                return false;
            }
        }
    }
    error.clear();
    return true;
}

bool RemoteDropboxProvider::Rename(
    const std::string& old_path,
    const std::string& new_path,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }
    Json request = Json::object();
    request["from_path"] = DropboxApiPathFromRemotePath(old_path);
    request["to_path"] = DropboxApiPathFromRemotePath(new_path);
    request["autorename"] = false;
    request["allow_shared_folder"] = false;
    request["allow_ownership_transfer"] = false;

    HttpResult result = PostJson(
        std::string(kDropboxApiHost) + "/files/move_v2",
        access_token_,
        token_type_,
        request);
    if (!result.ok) {
        const std::string hint = DropboxScopeErrorHint(result.body);
        error = hint.empty() ? BuildHttpError("Dropbox rename failed.", result) : hint;
        return false;
    }
    error.clear();
    return true;
}

bool RemoteDropboxProvider::RemoveFile(const std::string& path, std::string& error) {
    return DeletePath(path, error);
}

bool RemoteDropboxProvider::MakeDirectory(const std::string& path, std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }
    Json request = Json::object();
    request["path"] = DropboxApiPathFromRemotePath(path);
    request["autorename"] = false;

    HttpResult result = PostJson(
        std::string(kDropboxApiHost) + "/files/create_folder_v2",
        access_token_,
        token_type_,
        request);
    if (!result.ok) {
        const std::string hint = DropboxScopeErrorHint(result.body);
        error = hint.empty() ? BuildHttpError("Dropbox mkdir failed.", result) : hint;
        return false;
    }
    error.clear();
    return true;
}

bool RemoteDropboxProvider::RemoveDirectory(const std::string& path, std::string& error) {
    return DeletePath(path, error);
}

std::string RemoteDropboxProvider::DropboxApiPathFromRemotePath(const std::string& path) {
    std::string normalized = NormalizeRemoteDirectory(path.empty() ? "/" : path);
    if (normalized.empty() || normalized == "." || normalized == "/") {
        return "";
    }
    if (!normalized.empty() && normalized.front() != '/') {
        normalized = "/" + normalized;
    }
    return normalized;
}

std::string RemoteDropboxProvider::RemotePathFromDropboxDisplayPath(const std::string& path) {
    if (path.empty()) {
        return "/";
    }
    std::string normalized = NormalizeRemoteDirectory(path);
    if (normalized.empty() || normalized == ".") {
        return "/";
    }
    if (!normalized.empty() && normalized.front() != '/') {
        normalized = "/" + normalized;
    }
    return normalized;
}

bool RemoteDropboxProvider::DeletePath(const std::string& path, std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }
    Json request = Json::object();
    request["path"] = DropboxApiPathFromRemotePath(path);

    HttpResult result = PostJson(
        std::string(kDropboxApiHost) + "/files/delete_v2",
        access_token_,
        token_type_,
        request);
    if (!result.ok) {
        const std::string hint = DropboxScopeErrorHint(result.body);
        error = hint.empty() ? BuildHttpError("Dropbox delete failed.", result) : hint;
        return false;
    }
    error.clear();
    return true;
}

bool RemoteDropboxProvider::MakeDirectoryIfNeeded(const std::string& path, std::string& error) {
    if (DropboxApiPathFromRemotePath(path).empty()) {
        error.clear();
        return true;
    }
    if (MakeDirectory(path, error)) {
        return true;
    }
    if (error.find("path/conflict/folder") != std::string::npos ||
        error.find("path/conflict") != std::string::npos) {
        error.clear();
        return true;
    }
    return false;
}

bool RemoteDropboxProvider::EnsureConnected(std::string& error) const {
    if (access_token_.empty()) {
        error = "Dropbox provider is not connected. Add an access_token to the token file and reconnect.";
        return false;
    }
    error.clear();
    return true;
}

} // namespace textlt
