#include "curl_manager.hpp"

#include <chrono>
#include <cstdio>
#include <mutex>

#include <curl/curl.h>

namespace textlt {
namespace {

size_t WriteStringCallback(char* data, size_t size, size_t count, void* user_data) {
    auto* output = static_cast<std::string*>(user_data);
    output->append(data, size * count);
    return size * count;
}

size_t WriteFileCallback(char* data, size_t size, size_t count, void* user_data) {
    FILE* file = static_cast<FILE*>(user_data);
    return std::fwrite(data, size, count, file);
}

struct ProgressContext {
    CurlManager::ProgressCallback* callback = nullptr;
};

int CurlProgressCallback(void* client,
                         curl_off_t total,
                         curl_off_t downloaded,
                         curl_off_t,
                         curl_off_t) {
    auto* context = static_cast<ProgressContext*>(client);
    if (!context || !context->callback || !*context->callback) {
        return 0;
    }

    const bool should_continue = (*context->callback)(
        total > 0 ? static_cast<unsigned long long>(total) : 0,
        downloaded > 0 ? static_cast<unsigned long long>(downloaded) : 0);
    return should_continue ? 0 : 1;
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

} // namespace

void CurlManager::EnsureInitialized() {
    static std::once_flag init_once;
    std::call_once(init_once, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

CurlManager::Response CurlManager::Get(const std::string& url) {
    return Get(url, RequestOptions{});
}

CurlManager::Response CurlManager::Get(const std::string& url,
                                       const RequestOptions& options) {
    EnsureInitialized();

    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {};
    }

    CurlHeaders headers;
    for (const std::string& header : options.headers) {
        headers.Append(header);
    }
    if (options.no_cache) {
        headers.Append("Cache-Control: no-cache");
        headers.Append("Pragma: no-cache");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    if (headers.Get()) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.Get());
    }
    if (options.fresh_connect) {
        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
    }

    const CURLcode result = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    return {
        result == CURLE_OK && response_code < 400,
        response_code,
        std::move(response),
    };
}

bool CurlManager::DownloadToFile(const std::string& url,
                                 const std::filesystem::path& path,
                                 ProgressCallback progress) {
    EnsureInitialized();

    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) {
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(file);
        return false;
    }

    ProgressContext context{&progress};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    if (progress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context);
    }

    const CURLcode result = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);
    const bool close_ok = std::fclose(file) == 0;

    return result == CURLE_OK && close_ok && response_code < 400;
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
