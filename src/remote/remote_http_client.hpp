#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace textlt {

class RemoteCommandControl;

struct RemoteHttpResponse {
    bool ok = false;
    long status_code = 0;
    std::string body;
    std::string error;
};

class RemoteHttpClient {
public:
    using OutputCallback = std::function<void(const std::string& chunk)>;

    RemoteHttpResponse Request(
        const std::string& method,
        const std::string& url,
        const std::vector<std::string>& headers,
        const std::string& request_body = {},
        int timeout_seconds = 0) const;

    RemoteHttpResponse RequestStreaming(
        const std::string& method,
        const std::string& url,
        const std::vector<std::string>& headers,
        const std::string& request_body,
        int timeout_seconds,
        const std::atomic<bool>* cancel_requested,
        OutputCallback on_body_chunk,
        RemoteCommandControl* command_control = nullptr) const;

    RemoteHttpResponse Download(
        const std::string& method,
        const std::string& url,
        const std::vector<std::string>& headers,
        const std::filesystem::path& output_path,
        const std::string& request_body = {},
        int timeout_seconds = 0) const;

    RemoteHttpResponse DownloadCancelable(
        const std::string& method,
        const std::string& url,
        const std::vector<std::string>& headers,
        const std::filesystem::path& output_path,
        const std::string& request_body,
        int timeout_seconds,
        const std::atomic<bool>* cancel_requested,
        RemoteCommandControl* command_control = nullptr) const;

    RemoteHttpResponse UploadFile(
        const std::string& method,
        const std::string& url,
        const std::vector<std::string>& headers,
        const std::filesystem::path& input_path,
        int timeout_seconds = 0) const;

    static std::string UrlEncode(const std::string& value);
    static bool CheckCurlExecutable(std::string& error);

private:
    RemoteHttpResponse RunCurl(
        const std::string& method,
        const std::string& url,
        const std::vector<std::string>& headers,
        const std::filesystem::path* output_path,
        const std::filesystem::path* upload_file_path,
        const std::string* request_body,
        int timeout_seconds,
        const std::atomic<bool>* cancel_requested,
        OutputCallback on_body_chunk,
        RemoteCommandControl* command_control = nullptr) const;
};

} // namespace textlt
