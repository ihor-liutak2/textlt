#include "remote/remote_microsoft_drive_provider.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>


#include "json_utils.hpp"
#include "remote/remote_http_client.hpp"
#include "remote/remote_oauth_token_store.hpp"

namespace textlt {
namespace {

constexpr const char* kGraphApiHost = "https://graph.microsoft.com/v1.0";
constexpr const char* kUserAgent = "textlt/1.0";

using HttpResult = RemoteHttpResponse;

std::string TrimForError(std::string value, size_t max_size = 1000) {
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

std::string UrlEncode(const std::string& value) {
    return RemoteHttpClient::UrlEncode(value);
}

HttpResult HttpRequest(
    const std::string& method,
    const std::string& url,
    const std::string& access_token,
    const std::string& token_type,
    const std::vector<std::string>& extra_headers = {},
    const std::string& request_body = {}) {
    std::vector<std::string> headers;
    headers.push_back(AuthorizationHeader(token_type, access_token));
    headers.insert(headers.end(), extra_headers.begin(), extra_headers.end());

    RemoteHttpClient client;
    return client.Request(method, url, headers, request_body, 0);
}

HttpResult JsonRequest(
    const std::string& method,
    const std::string& url,
    const std::string& access_token,
    const std::string& token_type,
    const Json& request_body) {
    return HttpRequest(
        method,
        url,
        access_token,
        token_type,
        {"Content-Type: application/json"},
        request_body.dump());
}

HttpResult DownloadToFile(
    const std::string& url,
    const std::string& access_token,
    const std::string& token_type,
    const std::filesystem::path& local_path) {
    RemoteHttpClient client;
    return client.Download(
        "GET",
        url,
        {AuthorizationHeader(token_type, access_token)},
        local_path,
        {},
        300);
}

bool ReadWholeFile(const std::filesystem::path& path, std::string& content, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Cannot open local file for upload: " + path.string();
        return false;
    }
    content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (!file.good() && !file.eof()) {
        error = "Cannot read local file: " + path.string();
        return false;
    }
    error.clear();
    return true;
}

std::vector<std::string> SplitRemotePath(const std::string& path) {
    const std::string normalized = RemoteMicrosoftDriveProvider::MicrosoftRemotePathFromDisplayPath(path);
    std::vector<std::string> parts;
    std::string current;
    for (char ch : normalized) {
        if (ch == '/') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        current += ch;
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

std::uintmax_t GraphEntrySize(const Json& entry) {
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

RemoteMicrosoftDriveProvider::DriveItem ParseDriveItem(const Json& object) {
    RemoteMicrosoftDriveProvider::DriveItem item;
    item.id = JsonString(object, "id");
    item.name = JsonString(object, "name");
    item.size = GraphEntrySize(object);
    item.is_folder = object.find("folder") != object.end() && object["folder"].is_object();
    item.is_file = object.find("file") != object.end() && object["file"].is_object();
    return item;
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

RemoteEntryType MicrosoftEntryType(const RemoteMicrosoftDriveProvider::DriveItem& item) {
    if (item.is_folder) {
        return RemoteEntryType::Directory;
    }
    if (item.is_file) {
        return RemoteEntryType::File;
    }
    return RemoteEntryType::Other;
}

std::string AppendSelectParameter(const std::string& url) {
    return url + (url.find('?') == std::string::npos ? "?" : "&") + "$select=id,name,size,file,folder,webUrl";
}

} // namespace

bool RemoteMicrosoftDriveProvider::Connect(const RemoteConnectionConfig& config, std::string& error) {
    config_ = config;
    config_.remote_root = NormalizeRemoteDirectory(config_.remote_root.empty() ? "/" : config_.remote_root);
    access_token_.clear();
    token_type_ = "Bearer";

    if (config_.type != RemoteConnectionType::MicrosoftDrive) {
        error = "RemoteMicrosoftDriveProvider can only use Microsoft OneDrive / SharePoint connections.";
        return false;
    }

    const std::filesystem::path token_path = ExpandRemoteUserPath(config_.token_file);
    if (token_path.empty()) {
        error = "Microsoft connection needs a token file with an access_token.";
        return false;
    }

    RemoteOAuthToken token;
    RemoteOAuthTokenStore store(token_path);
    if (!store.Load(token, error)) {
        return false;
    }
    if (token.access_token.empty()) {
        error = "Microsoft token file is a placeholder. Add an access_token before using OneDrive / SharePoint files.";
        return false;
    }

    access_token_ = token.access_token;
    token_type_ = token.token_type.empty() ? "Bearer" : token.token_type;
    error.clear();
    return true;
}

bool RemoteMicrosoftDriveProvider::TestConnection(std::string& output, std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    HttpResult result = HttpRequest("GET", AppendSelectParameter(DriveBaseUrl()), access_token_, token_type_);
    if (!result.ok) {
        error = BuildHttpError("Microsoft Graph connection test failed.", result);
        return false;
    }

    const Json parsed = Json::parse(result.body, nullptr, false);
    output = "Microsoft OneDrive / SharePoint connection works.";
    if (!parsed.is_discarded() && parsed.is_object()) {
        const std::string name = JsonString(parsed, "name");
        const std::string id = JsonString(parsed, "id");
        const std::string web_url = JsonString(parsed, "webUrl");
        if (!name.empty()) {
            output += "\nDrive: " + name;
        }
        if (!id.empty()) {
            output += "\nDrive ID: " + id;
        }
        if (!web_url.empty()) {
            output += "\nURL: " + web_url;
        }
    }
    error.clear();
    return true;
}

bool RemoteMicrosoftDriveProvider::List(
    const std::string& path,
    std::vector<RemoteEntry>& entries,
    std::string& error) {
    entries.clear();
    if (!EnsureConnected(error)) {
        return false;
    }

    const std::string display_path = MicrosoftRemotePathFromDisplayPath(path.empty() ? config_.remote_root : path);
    std::string url = ChildrenUrlForPath(display_path) + "?$select=id,name,size,file,folder&$top=999";
    for (;;) {
        HttpResult result = HttpRequest("GET", url, access_token_, token_type_);
        if (!result.ok) {
            error = BuildHttpError("Cannot list Microsoft drive directory.", result);
            return false;
        }

        const Json parsed = Json::parse(result.body, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            error = "Microsoft Graph children request returned invalid JSON.";
            return false;
        }

        const auto values_iter = parsed.find("value");
        if (values_iter != parsed.end() && values_iter->is_array()) {
            for (const Json& object : *values_iter) {
                if (!object.is_object()) {
                    continue;
                }
                DriveItem item = ParseDriveItem(object);
                if (item.name.empty()) {
                    continue;
                }
                RemoteEntry entry;
                entry.name = item.name;
                entry.path = JoinRemotePath(display_path, item.name);
                entry.type = MicrosoftEntryType(item);
                entry.size = item.size;
                entry.hidden = !entry.name.empty() && entry.name.front() == '.';
                entries.push_back(std::move(entry));
            }
        }

        url = JsonString(parsed, "@odata.nextLink");
        if (url.empty()) {
            break;
        }
    }

    SortRemoteEntries(entries);
    error.clear();
    return true;
}

bool RemoteMicrosoftDriveProvider::Download(
    const std::string& remote_path,
    const std::string& local_path,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    DriveItem item;
    if (!ResolvePath(remote_path, item, error)) {
        return false;
    }
    if (item.is_folder) {
        error = "Microsoft drive path is a folder. Use folder download.";
        return false;
    }
    if (!item.is_file) {
        error = "Microsoft drive item is not a downloadable ordinary file.";
        return false;
    }

    HttpResult result = DownloadToFile(ContentUrlForPath(remote_path), access_token_, token_type_, std::filesystem::path(local_path));
    if (!result.ok) {
        error = BuildHttpError("Microsoft Graph download failed.", result);
        return false;
    }
    error.clear();
    return true;
}

bool RemoteMicrosoftDriveProvider::Upload(
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

    const std::string parent_path = RemoteParentPath(remote_path);
    const std::string file_name = RemoteBaseName(remote_path);
    if (file_name.empty()) {
        error = "Remote upload target file name is empty.";
        return false;
    }
    if (!CreateFolderIfNeeded(parent_path.empty() ? "/" : parent_path, error)) {
        return false;
    }

    DriveItem existing;
    if (ResolvePath(remote_path, existing, error)) {
        if (existing.is_folder) {
            error = "Remote upload target is a Microsoft drive folder.";
            return false;
        }
    } else if (error.rfind("Microsoft drive item not found:", 0) != 0) {
        return false;
    }

    std::string file_content;
    if (!ReadWholeFile(std::filesystem::path(local_path), file_content, error)) {
        return false;
    }

    HttpResult result = HttpRequest(
        "PUT",
        ContentUrlForPath(remote_path),
        access_token_,
        token_type_,
        {"Content-Type: application/octet-stream"},
        file_content);
    if (!result.ok) {
        error = BuildHttpError("Microsoft Graph upload failed.", result);
        return false;
    }
    error.clear();
    return true;
}

bool RemoteMicrosoftDriveProvider::DownloadDirectory(
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

bool RemoteMicrosoftDriveProvider::UploadDirectory(
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

    if (!CreateFolderIfNeeded(remote_path, error)) {
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

bool RemoteMicrosoftDriveProvider::Rename(
    const std::string& old_path,
    const std::string& new_path,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    if (MicrosoftRemotePathFromDisplayPath(RemoteParentPath(old_path)) !=
        MicrosoftRemotePathFromDisplayPath(RemoteParentPath(new_path))) {
        error = "Microsoft provider currently supports rename inside the same folder. Move support will be a later patch.";
        return false;
    }

    DriveItem item;
    if (!ResolvePath(old_path, item, error)) {
        return false;
    }

    const std::string new_name = RemoteBaseName(new_path);
    if (new_name.empty()) {
        error = "New Microsoft drive name is empty.";
        return false;
    }

    Json request = Json::object();
    request["name"] = new_name;
    HttpResult result = JsonRequest("PATCH", ItemUrlForPath(old_path), access_token_, token_type_, request);
    if (!result.ok) {
        error = BuildHttpError("Microsoft Graph rename failed.", result);
        return false;
    }
    error.clear();
    return true;
}

bool RemoteMicrosoftDriveProvider::RemoveFile(const std::string& path, std::string& error) {
    return DeletePath(path, error);
}

bool RemoteMicrosoftDriveProvider::MakeDirectory(const std::string& path, std::string& error) {
    return CreateFolderIfNeeded(path, error);
}

bool RemoteMicrosoftDriveProvider::RemoveDirectory(const std::string& path, std::string& error) {
    return DeletePath(path, error);
}

std::string RemoteMicrosoftDriveProvider::MicrosoftRemotePathFromDisplayPath(const std::string& path) {
    std::string normalized = NormalizeRemoteDirectory(path.empty() ? "/" : path);
    if (normalized.empty() || normalized == ".") {
        return "/";
    }
    if (!normalized.empty() && normalized.front() != '/') {
        normalized = "/" + normalized;
    }
    return normalized;
}

std::string RemoteMicrosoftDriveProvider::MicrosoftDriveBasePathFromConfig(const RemoteConnectionConfig& config) {
    if (!config.drive_id.empty()) {
        return "/drives/" + UrlEncode(config.drive_id);
    }
    if (!config.site_id.empty()) {
        return "/sites/" + UrlEncode(config.site_id) + "/drive";
    }
    return "/me/drive";
}

std::string RemoteMicrosoftDriveProvider::MicrosoftEncodePathSegments(const std::string& path) {
    std::string normalized = MicrosoftRemotePathFromDisplayPath(path);
    std::string encoded;
    bool first = true;
    for (const std::string& part : SplitRemotePath(normalized)) {
        if (!first) {
            encoded += "/";
        }
        encoded += UrlEncode(part);
        first = false;
    }
    return encoded;
}

bool RemoteMicrosoftDriveProvider::EnsureConnected(std::string& error) const {
    if (access_token_.empty()) {
        error = "Microsoft provider is not connected. Add an access_token to the token file and reconnect.";
        return false;
    }
    error.clear();
    return true;
}

std::string RemoteMicrosoftDriveProvider::DriveBaseUrl() const {
    return std::string(kGraphApiHost) + MicrosoftDriveBasePathFromConfig(config_);
}

std::string RemoteMicrosoftDriveProvider::ItemUrlForPath(const std::string& remote_path) const {
    const std::string encoded_path = MicrosoftEncodePathSegments(remote_path);
    if (encoded_path.empty()) {
        return DriveBaseUrl() + "/root";
    }
    return DriveBaseUrl() + "/root:/" + encoded_path + ":";
}

std::string RemoteMicrosoftDriveProvider::ChildrenUrlForPath(const std::string& remote_path) const {
    return ItemUrlForPath(remote_path) + "/children";
}

std::string RemoteMicrosoftDriveProvider::ContentUrlForPath(const std::string& remote_path) const {
    return ItemUrlForPath(remote_path) + "/content";
}

bool RemoteMicrosoftDriveProvider::ResolvePath(
    const std::string& remote_path,
    DriveItem& item,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    HttpResult result = HttpRequest("GET", AppendSelectParameter(ItemUrlForPath(remote_path)), access_token_, token_type_);
    if (!result.ok) {
        if (result.status_code == 404) {
            error = "Microsoft drive item not found: " + MicrosoftRemotePathFromDisplayPath(remote_path);
        } else {
            error = BuildHttpError("Microsoft Graph item lookup failed.", result);
        }
        return false;
    }

    const Json parsed = Json::parse(result.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error = "Microsoft Graph item lookup returned invalid JSON.";
        return false;
    }
    item = ParseDriveItem(parsed);
    if (item.name.empty() && MicrosoftRemotePathFromDisplayPath(remote_path) != "/") {
        error = "Microsoft Graph item lookup returned an empty name.";
        return false;
    }
    error.clear();
    return true;
}

bool RemoteMicrosoftDriveProvider::CreateFolderIfNeeded(
    const std::string& remote_path,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    std::string current_path = "/";
    for (const std::string& part : SplitRemotePath(remote_path)) {
        const std::string child_path = JoinRemotePath(current_path, part);
        DriveItem existing;
        if (ResolvePath(child_path, existing, error)) {
            if (!existing.is_folder) {
                error = "Microsoft drive path exists but is not a folder: " + child_path;
                return false;
            }
            current_path = child_path;
            continue;
        }
        if (error.rfind("Microsoft drive item not found:", 0) != 0) {
            return false;
        }

        Json request = Json::object();
        request["name"] = part;
        request["folder"] = Json::object();
        request["@microsoft.graph.conflictBehavior"] = "fail";
        HttpResult result = JsonRequest("POST", ChildrenUrlForPath(current_path), access_token_, token_type_, request);
        if (!result.ok) {
            error = BuildHttpError("Microsoft Graph mkdir failed.", result);
            return false;
        }
        current_path = child_path;
    }

    error.clear();
    return true;
}

bool RemoteMicrosoftDriveProvider::DeletePath(const std::string& path, std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    if (MicrosoftRemotePathFromDisplayPath(path) == "/") {
        error = "Refusing to delete the Microsoft drive root folder.";
        return false;
    }

    HttpResult result = HttpRequest("DELETE", ItemUrlForPath(path), access_token_, token_type_);
    if (!result.ok) {
        error = BuildHttpError("Microsoft Graph delete failed.", result);
        return false;
    }
    error.clear();
    return true;
}

} // namespace textlt
