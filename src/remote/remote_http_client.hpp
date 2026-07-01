#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace textlt {

struct RemoteHttpResponse {
    bool ok = false;
    long status_code = 0;
    std::string body;
    std::string error;
};

class RemoteHttpClient {
public:
    RemoteHttpResponse Request(
        const std::string& method,
        const std::string& url,
        const std::vector<std::string>& headers,
        const std::string& request_body = {},
        int timeout_seconds = 0) const;

    RemoteHttpResponse Download(
        const std::string& method,
        const std::string& url,
        const std::vector<std::string>& headers,
        const std::filesystem::path& output_path,
        const std::string& request_body = {},
        int timeout_seconds = 0) const;

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
        int timeout_seconds) const;
};

} // namespace textlt
