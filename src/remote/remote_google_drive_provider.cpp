#include "remote/remote_google_drive_provider.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "json_utils.hpp"
#include "remote/remote_oauth_token_store.hpp"

namespace textlt {
namespace {

constexpr const char* kDriveApiHost = "https://www.googleapis.com/drive/v3";
constexpr const char* kDriveUploadHost = "https://www.googleapis.com/upload/drive/v3";
constexpr const char* kGoogleFolderMimeType = "application/vnd.google-apps.folder";
constexpr const char* kGoogleAppsMimePrefix = "application/vnd.google-apps.";
constexpr const char* kUserAgent = "textlt/1.0";

struct HttpResult {
    bool ok = false;
    long status_code = 0;
    std::string body;
    std::string error;
};

size_t WriteStringCallback(char* data, size_t size, size_t count, void* user_data) {
    auto* output = static_cast<std::string*>(user_data);
    output->append(data, size * count);
    return size * count;
}

size_t WriteFileCallback(char* data, size_t size, size_t count, void* user_data) {
    FILE* file = static_cast<FILE*>(user_data);
    return std::fwrite(data, size, count, file);
}

class CurlHeaders {
public:
    CurlHeaders() = default;
    CurlHeaders(const CurlHeaders&) = delete;
    CurlHeaders& operator=(const CurlHeaders&) = delete;
    ~CurlHeaders() {
        if (headers_) {
            curl_slist_free_all(headers_);
        }
    }

    void Append(const std::string& header) {
        headers_ = curl_slist_append(headers_, header.c_str());
    }

    curl_slist* Get() const { return headers_; }

private:
    curl_slist* headers_ = nullptr;
};

void EnsureCurlInitialized() {
    static std::once_flag init_once;
    std::call_once(init_once, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

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
    EnsureCurlInitialized();
    CURL* curl = curl_easy_init();
    if (!curl) {
        return value;
    }
    char* encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    std::string result = encoded ? encoded : value;
    if (encoded) {
        curl_free(encoded);
    }
    curl_easy_cleanup(curl);
    return result;
}

std::string EscapeDriveQueryString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\' || ch == '\'') {
            escaped += '\\';
        }
        escaped += ch;
    }
    return escaped;
}

std::string WithParameter(const std::string& url, const std::string& name, const std::string& value) {
    return url + (url.find('?') == std::string::npos ? "?" : "&") + name + "=" + UrlEncode(value);
}

HttpResult HttpRequest(
    const std::string& method,
    const std::string& url,
    const std::string& access_token,
    const std::string& token_type,
    const std::vector<std::string>& extra_headers = {},
    const std::string& request_body = {}) {
    EnsureCurlInitialized();

    HttpResult result;
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "Cannot initialize libcurl.";
        return result;
    }

    CurlHeaders headers;
    headers.Append(AuthorizationHeader(token_type, access_token));
    for (const std::string& header : extra_headers) {
        headers.Append(header);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.Get());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    }

    if (!request_body.empty() || method == "POST" || method == "PATCH") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request_body.size()));
    }

    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status_code);
    if (code != CURLE_OK) {
        result.error = curl_easy_strerror(code);
    }
    curl_easy_cleanup(curl);

    result.ok = code == CURLE_OK && result.status_code >= 200 && result.status_code < 300;
    return result;
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
    EnsureCurlInitialized();

    HttpResult result;
    std::error_code fs_error;
    const std::filesystem::path parent = local_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, fs_error);
        if (fs_error) {
            result.error = "Cannot create local directory: " + parent.string() + "\n" + fs_error.message();
            return result;
        }
    }

    FILE* file = std::fopen(local_path.string().c_str(), "wb");
    if (!file) {
        result.error = "Cannot open local file for writing: " + local_path.string();
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(file);
        result.error = "Cannot initialize libcurl.";
        return result;
    }

    CurlHeaders headers;
    headers.Append(AuthorizationHeader(token_type, access_token));

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.Get());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteStringCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &result.body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status_code);
    if (code != CURLE_OK) {
        result.error = curl_easy_strerror(code);
    }
    curl_easy_cleanup(curl);
    const bool close_ok = std::fclose(file) == 0;

    result.ok = code == CURLE_OK && close_ok && result.status_code >= 200 && result.status_code < 300;
    if (!result.ok) {
        std::filesystem::remove(local_path, fs_error);
        if (result.error.empty() && !close_ok) {
            result.error = "Cannot finish writing local file.";
        }
    }
    return result;
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
    const std::string normalized = RemoteGoogleDriveProvider::GoogleRemotePathFromDisplayPath(path);
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

std::uintmax_t GoogleEntrySize(const Json& entry) {
    const auto iter = entry.find("size");
    if (iter == entry.end()) {
        return 0;
    }
    if (iter->is_string()) {
        try {
            return static_cast<std::uintmax_t>(std::stoull(iter->get<std::string>()));
        } catch (...) {
            return 0;
        }
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

RemoteEntryType GoogleEntryType(const std::string& mime_type) {
    if (mime_type == kGoogleFolderMimeType) {
        return RemoteEntryType::Directory;
    }
    if (RemoteGoogleDriveProvider::IsGoogleWorkspaceMimeType(mime_type)) {
        return RemoteEntryType::Other;
    }
    return RemoteEntryType::File;
}

RemoteGoogleDriveProvider::DriveItem ParseDriveItem(const Json& object) {
    RemoteGoogleDriveProvider::DriveItem item;
    item.id = JsonString(object, "id");
    item.name = JsonString(object, "name");
    item.mime_type = JsonString(object, "mimeType");
    item.size = GoogleEntrySize(object);
    const auto parents_iter = object.find("parents");
    if (parents_iter != object.end() && parents_iter->is_array() && !parents_iter->empty() && parents_iter->front().is_string()) {
        item.parent_id = parents_iter->front().get<std::string>();
    }
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

Json DriveFileMetadata(const std::string& name, const std::string& parent_id, const std::string& mime_type = {}) {
    Json metadata = Json::object();
    metadata["name"] = name;
    if (!mime_type.empty()) {
        metadata["mimeType"] = mime_type;
    }
    if (!parent_id.empty()) {
        metadata["parents"] = Json::array({parent_id});
    }
    return metadata;
}

std::string BuildMultipartBody(
    const Json& metadata,
    const std::string& file_content,
    const std::string& boundary) {
    std::string body;
    body += "--" + boundary + "\r\n";
    body += "Content-Type: application/json; charset=UTF-8\r\n\r\n";
    body += metadata.dump();
    body += "\r\n--" + boundary + "\r\n";
    body += "Content-Type: application/octet-stream\r\n\r\n";
    body += file_content;
    body += "\r\n--" + boundary + "--\r\n";
    return body;
}

} // namespace

bool RemoteGoogleDriveProvider::Connect(const RemoteConnectionConfig& config, std::string& error) {
    config_ = config;
    config_.remote_root = NormalizeRemoteDirectory(config_.remote_root.empty() ? "/" : config_.remote_root);
    access_token_.clear();
    token_type_ = "Bearer";

    if (config_.type != RemoteConnectionType::GoogleDrive) {
        error = "RemoteGoogleDriveProvider can only use Google Drive connections.";
        return false;
    }

    const std::filesystem::path token_path = ExpandRemoteUserPath(config_.token_file);
    if (token_path.empty()) {
        error = "Google Drive connection needs a token file with an access_token.";
        return false;
    }

    RemoteOAuthToken token;
    RemoteOAuthTokenStore store(token_path);
    if (!store.Load(token, error)) {
        return false;
    }
    if (token.access_token.empty()) {
        error = "Google Drive token file is a placeholder. Add an access_token before using Google Drive files.";
        return false;
    }

    access_token_ = token.access_token;
    token_type_ = token.token_type.empty() ? "Bearer" : token.token_type;
    error.clear();
    return true;
}

bool RemoteGoogleDriveProvider::TestConnection(std::string& output, std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    HttpResult result = HttpRequest(
        "GET",
        std::string(kDriveApiHost) + "/about?fields=user(displayName,emailAddress)",
        access_token_,
        token_type_);
    if (!result.ok) {
        error = BuildHttpError("Google Drive connection test failed.", result);
        return false;
    }

    const Json parsed = Json::parse(result.body, nullptr, false);
    output = "Google Drive connection works.";
    if (!parsed.is_discarded() && parsed.is_object()) {
        const Json& user = parsed.contains("user") ? parsed["user"] : Json::object();
        if (user.is_object()) {
            const std::string display_name = JsonString(user, "displayName");
            const std::string email = JsonString(user, "emailAddress");
            if (!display_name.empty()) {
                output += "\nName: " + display_name;
            }
            if (!email.empty()) {
                output += "\nEmail: " + email;
            }
        }
    }
    error.clear();
    return true;
}

bool RemoteGoogleDriveProvider::List(
    const std::string& path,
    std::vector<RemoteEntry>& entries,
    std::string& error) {
    entries.clear();
    if (!EnsureConnected(error)) {
        return false;
    }

    const std::string display_path = GoogleRemotePathFromDisplayPath(path.empty() ? config_.remote_root : path);
    std::string folder_id;
    if (!ResolveDirectoryId(display_path, folder_id, error)) {
        return false;
    }

    std::string page_token;
    for (;;) {
        const std::string query = "'" + EscapeDriveQueryString(folder_id) + "' in parents and trashed = false";
        std::string url = std::string(kDriveApiHost) + "/files?pageSize=1000&supportsAllDrives=true&includeItemsFromAllDrives=true";
        url = WithParameter(url, "q", query);
        url = WithParameter(url, "fields", "nextPageToken,files(id,name,mimeType,size,parents)");
        if (!page_token.empty()) {
            url = WithParameter(url, "pageToken", page_token);
        }

        HttpResult result = HttpRequest("GET", url, access_token_, token_type_);
        if (!result.ok) {
            error = BuildHttpError("Cannot list Google Drive directory.", result);
            return false;
        }

        const Json parsed = Json::parse(result.body, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            error = "Google Drive files.list returned invalid JSON.";
            return false;
        }

        const auto files_iter = parsed.find("files");
        if (files_iter != parsed.end() && files_iter->is_array()) {
            for (const Json& object : *files_iter) {
                if (!object.is_object()) {
                    continue;
                }
                DriveItem item = ParseDriveItem(object);
                if (item.id.empty() || item.name.empty()) {
                    continue;
                }
                RemoteEntry entry;
                entry.name = item.name;
                entry.path = JoinRemotePath(display_path, item.name);
                entry.type = GoogleEntryType(item.mime_type);
                entry.size = item.size;
                entry.hidden = !entry.name.empty() && entry.name.front() == '.';
                entries.push_back(std::move(entry));
            }
        }

        page_token = JsonString(parsed, "nextPageToken");
        if (page_token.empty()) {
            break;
        }
    }

    SortRemoteEntries(entries);
    error.clear();
    return true;
}

bool RemoteGoogleDriveProvider::Download(
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
    if (item.mime_type == kGoogleFolderMimeType) {
        error = "Google Drive path is a folder. Use folder download.";
        return false;
    }
    if (IsGoogleWorkspaceMimeType(item.mime_type)) {
        error = "Google Workspace documents cannot be downloaded with alt=media in this first provider version. Export support will be a later patch.";
        return false;
    }

    std::string url = std::string(kDriveApiHost) + "/files/" + UrlEncode(item.id) + "?alt=media&supportsAllDrives=true";
    HttpResult result = DownloadToFile(url, access_token_, token_type_, std::filesystem::path(local_path));
    if (!result.ok) {
        error = BuildHttpError("Google Drive download failed.", result);
        return false;
    }
    error.clear();
    return true;
}

bool RemoteGoogleDriveProvider::Upload(
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

    std::string file_content;
    if (!ReadWholeFile(std::filesystem::path(local_path), file_content, error)) {
        return false;
    }

    const std::string parent_path = RemoteParentPath(remote_path);
    const std::string file_name = RemoteBaseName(remote_path);
    if (file_name.empty()) {
        error = "Remote upload target file name is empty.";
        return false;
    }

    std::string parent_id;
    if (!CreateFolderIfNeeded(parent_path.empty() ? "/" : parent_path, parent_id, error)) {
        return false;
    }

    DriveItem existing;
    const bool found_existing = FindChildByName(parent_id, file_name, false, existing, error);
    if (!found_existing && !error.empty() && error.rfind("Google Drive item not found:", 0) != 0) {
        return false;
    }
    if (found_existing && existing.mime_type == kGoogleFolderMimeType) {
        error = "Remote upload target is a Google Drive folder.";
        return false;
    }
    if (found_existing && IsGoogleWorkspaceMimeType(existing.mime_type)) {
        error = "Remote upload target is a Google Workspace document. Replacing Google Docs/Sheets/Slides is not supported in this provider version.";
        return false;
    }

    if (found_existing) {
        const std::string url = std::string(kDriveUploadHost) + "/files/" + UrlEncode(existing.id) +
            "?uploadType=media&supportsAllDrives=true";
        HttpResult result = HttpRequest(
            "PATCH",
            url,
            access_token_,
            token_type_,
            {"Content-Type: application/octet-stream"},
            file_content);
        if (!result.ok) {
            error = BuildHttpError("Google Drive upload update failed.", result);
            return false;
        }
        error.clear();
        return true;
    }

    const std::string boundary = "textlt_google_drive_boundary_20260701";
    const Json metadata = DriveFileMetadata(file_name, parent_id);
    const std::string body = BuildMultipartBody(metadata, file_content, boundary);
    const std::string url = std::string(kDriveUploadHost) +
        "/files?uploadType=multipart&supportsAllDrives=true&fields=id,name";
    HttpResult result = HttpRequest(
        "POST",
        url,
        access_token_,
        token_type_,
        {"Content-Type: multipart/related; boundary=" + boundary},
        body);
    if (!result.ok) {
        error = BuildHttpError("Google Drive upload create failed.", result);
        return false;
    }
    error.clear();
    return true;
}

bool RemoteGoogleDriveProvider::DownloadDirectory(
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

bool RemoteGoogleDriveProvider::UploadDirectory(
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

    std::string folder_id;
    if (!CreateFolderIfNeeded(remote_path, folder_id, error)) {
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

bool RemoteGoogleDriveProvider::Rename(
    const std::string& old_path,
    const std::string& new_path,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    DriveItem item;
    if (!ResolvePath(old_path, item, error)) {
        return false;
    }

    const std::string new_name = RemoteBaseName(new_path);
    if (new_name.empty()) {
        error = "New Google Drive name is empty.";
        return false;
    }

    Json request = Json::object();
    request["name"] = new_name;

    std::string url = std::string(kDriveApiHost) + "/files/" + UrlEncode(item.id) +
        "?supportsAllDrives=true&fields=id,name";
    HttpResult result = JsonRequest("PATCH", url, access_token_, token_type_, request);
    if (!result.ok) {
        error = BuildHttpError("Google Drive rename failed.", result);
        return false;
    }
    error.clear();
    return true;
}

bool RemoteGoogleDriveProvider::RemoveFile(const std::string& path, std::string& error) {
    return DeletePath(path, error);
}

bool RemoteGoogleDriveProvider::MakeDirectory(const std::string& path, std::string& error) {
    std::string folder_id;
    return CreateFolderIfNeeded(path, folder_id, error);
}

bool RemoteGoogleDriveProvider::RemoveDirectory(const std::string& path, std::string& error) {
    return DeletePath(path, error);
}

std::string RemoteGoogleDriveProvider::GoogleRemotePathFromDisplayPath(const std::string& path) {
    std::string normalized = NormalizeRemoteDirectory(path.empty() ? "/" : path);
    if (normalized.empty() || normalized == ".") {
        return "/";
    }
    if (!normalized.empty() && normalized.front() != '/') {
        normalized = "/" + normalized;
    }
    return normalized;
}

std::string RemoteGoogleDriveProvider::GoogleRootIdFromConfig(const RemoteConnectionConfig& config) {
    return config.root_folder_id.empty() ? "root" : config.root_folder_id;
}

bool RemoteGoogleDriveProvider::IsGoogleWorkspaceMimeType(const std::string& mime_type) {
    return mime_type.rfind(kGoogleAppsMimePrefix, 0) == 0 && mime_type != kGoogleFolderMimeType;
}

bool RemoteGoogleDriveProvider::EnsureConnected(std::string& error) const {
    if (access_token_.empty()) {
        error = "Google Drive provider is not connected. Add an access_token to the token file and reconnect.";
        return false;
    }
    error.clear();
    return true;
}

bool RemoteGoogleDriveProvider::ResolveDirectoryId(
    const std::string& remote_path,
    std::string& folder_id,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    folder_id = GoogleRootIdFromConfig(config_);
    for (const std::string& part : SplitRemotePath(remote_path)) {
        DriveItem child;
        if (!FindChildByName(folder_id, part, true, child, error)) {
            return false;
        }
        folder_id = child.id;
    }
    error.clear();
    return true;
}

bool RemoteGoogleDriveProvider::ResolvePath(
    const std::string& remote_path,
    DriveItem& item,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    const std::vector<std::string> parts = SplitRemotePath(remote_path);
    if (parts.empty()) {
        item = DriveItem{};
        item.id = GoogleRootIdFromConfig(config_);
        item.name = "/";
        item.mime_type = kGoogleFolderMimeType;
        error.clear();
        return true;
    }

    std::string parent_id = GoogleRootIdFromConfig(config_);
    for (size_t index = 0; index < parts.size(); ++index) {
        DriveItem child;
        const bool directory_only = index + 1 < parts.size();
        if (!FindChildByName(parent_id, parts[index], directory_only, child, error)) {
            return false;
        }
        parent_id = child.id;
        item = child;
    }
    error.clear();
    return true;
}

bool RemoteGoogleDriveProvider::FindChildByName(
    const std::string& parent_id,
    const std::string& name,
    bool directory_only,
    DriveItem& item,
    std::string& error) {
    if (name.empty()) {
        error = "Google Drive item name is empty.";
        return false;
    }

    std::string query = "'" + EscapeDriveQueryString(parent_id) + "' in parents and name = '" +
        EscapeDriveQueryString(name) + "' and trashed = false";
    if (directory_only) {
        query += " and mimeType = '";
        query += kGoogleFolderMimeType;
        query += "'";
    }

    std::string url = std::string(kDriveApiHost) + "/files?pageSize=10&supportsAllDrives=true&includeItemsFromAllDrives=true";
    url = WithParameter(url, "q", query);
    url = WithParameter(url, "fields", "files(id,name,mimeType,size,parents)");

    HttpResult result = HttpRequest("GET", url, access_token_, token_type_);
    if (!result.ok) {
        error = BuildHttpError("Google Drive lookup failed.", result);
        return false;
    }

    const Json parsed = Json::parse(result.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error = "Google Drive files.list lookup returned invalid JSON.";
        return false;
    }

    const auto files_iter = parsed.find("files");
    if (files_iter == parsed.end() || !files_iter->is_array() || files_iter->empty()) {
        error = "Google Drive item not found: " + name;
        return false;
    }

    item = ParseDriveItem(files_iter->front());
    if (item.id.empty()) {
        error = "Google Drive item lookup returned an empty id: " + name;
        return false;
    }
    error.clear();
    return true;
}

bool RemoteGoogleDriveProvider::CreateFolderIfNeeded(
    const std::string& remote_path,
    std::string& folder_id,
    std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    folder_id = GoogleRootIdFromConfig(config_);
    for (const std::string& part : SplitRemotePath(remote_path)) {
        DriveItem child;
        if (FindChildByName(folder_id, part, true, child, error)) {
            folder_id = child.id;
            continue;
        }
        if (error.rfind("Google Drive item not found:", 0) != 0) {
            return false;
        }

        Json metadata = DriveFileMetadata(part, folder_id, kGoogleFolderMimeType);
        const std::string url = std::string(kDriveApiHost) + "/files?supportsAllDrives=true&fields=id,name,mimeType,parents";
        HttpResult result = JsonRequest("POST", url, access_token_, token_type_, metadata);
        if (!result.ok) {
            error = BuildHttpError("Google Drive mkdir failed.", result);
            return false;
        }
        const Json parsed = Json::parse(result.body, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            error = "Google Drive mkdir returned invalid JSON.";
            return false;
        }
        child = ParseDriveItem(parsed);
        if (child.id.empty()) {
            error = "Google Drive mkdir returned an empty folder id.";
            return false;
        }
        folder_id = child.id;
    }

    error.clear();
    return true;
}

bool RemoteGoogleDriveProvider::DeletePath(const std::string& path, std::string& error) {
    if (!EnsureConnected(error)) {
        return false;
    }

    DriveItem item;
    if (!ResolvePath(path, item, error)) {
        return false;
    }
    if (item.id == GoogleRootIdFromConfig(config_)) {
        error = "Refusing to delete the Google Drive root folder.";
        return false;
    }

    const std::string url = std::string(kDriveApiHost) + "/files/" + UrlEncode(item.id) + "?supportsAllDrives=true";
    HttpResult result = HttpRequest("DELETE", url, access_token_, token_type_);
    if (!result.ok) {
        error = BuildHttpError("Google Drive delete failed.", result);
        return false;
    }
    error.clear();
    return true;
}

} // namespace textlt
