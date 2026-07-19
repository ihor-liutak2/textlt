#include "curl_manager.hpp"

#include <chrono>
#include <filesystem>
#include <system_error>

#include "remote/remote_http_client.hpp"

namespace textlt {

void CurlManager::EnsureInitialized() {
    // The application uses the external curl executable now.
}

CurlManager::Response CurlManager::Get(const std::string& url) {
    return Get(url, RequestOptions{});
}

CurlManager::Response CurlManager::Get(const std::string& url,
                                       const RequestOptions& options) {
    return Get(url, options, nullptr, nullptr);
}

CurlManager::Response CurlManager::Get(
    const std::string& url,
    const RequestOptions& options,
    const std::atomic<bool>* cancel_requested,
    RemoteCommandControl* command_control) {
    std::vector<std::string> headers = options.headers;
    if (options.no_cache) {
        headers.push_back("Cache-Control: no-cache");
        headers.push_back("Pragma: no-cache");
    }
    if (options.fresh_connect) {
        headers.push_back("Cache-Control: no-cache");
    }

    RemoteHttpClient client;
    const RemoteHttpResponse response = cancel_requested
        ? client.RequestStreaming("GET", url, headers, {}, 0, cancel_requested, {}, command_control)
        : client.Request("GET", url, headers, {}, 0);
    return {
        response.ok,
        response.status_code,
        response.body,
    };
}

bool CurlManager::DownloadToFile(const std::string& url,
                                 const std::filesystem::path& path,
                                 ProgressCallback progress,
                                 const std::atomic<bool>* cancel_requested,
                                 RemoteCommandControl* command_control) {
    if (progress && !progress(0, 0)) {
        return false;
    }

    RemoteHttpClient client;
    const RemoteHttpResponse response = cancel_requested
        ? client.DownloadCancelable(
              "GET", url, {}, path, {}, 0, cancel_requested, command_control)
        : client.Download("GET", url, {}, path, {}, 0);
    if (!response.ok) {
        return false;
    }

    if (progress) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        const unsigned long long downloaded = error ? 0 : static_cast<unsigned long long>(size);
        progress(downloaded, downloaded);
    }
    return true;
}

std::string CurlManager::WithCacheBust(const std::string& url) {
    const auto cache_bust =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    return url + (url.find('?') == std::string::npos ? "?" : "&") +
           "_textlt_cache_bust=" + std::to_string(cache_bust);
}

} // namespace textlt
