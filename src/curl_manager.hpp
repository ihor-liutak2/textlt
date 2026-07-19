#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace textlt {

class RemoteCommandControl;

class CurlManager {
public:
    static constexpr const char* kUserAgent = "textlt/1.0";
    static constexpr const char* kPiperRegistryUrl =
        "https://ihor-liutak2.github.io/textlt/registries/piper_voices_index.json";
    static constexpr const char* kAiRegistryUrl =
        "https://ihor-liutak2.github.io/textlt/registries/ollama_models_index.json";

    struct Response {
        bool ok = false;
        long status_code = 0;
        std::string body;
    };

    struct RequestOptions {
        bool no_cache = false;
        bool fresh_connect = false;
        std::vector<std::string> headers;
    };

    using ProgressCallback = std::function<bool(unsigned long long total_bytes,
                                                unsigned long long downloaded_bytes)>;

    static Response Get(const std::string& url);
    static Response Get(const std::string& url, const RequestOptions& options);
    static Response Get(const std::string& url,
                        const RequestOptions& options,
                        const std::atomic<bool>* cancel_requested,
                        RemoteCommandControl* command_control = nullptr);
    static bool DownloadToFile(const std::string& url,
                               const std::filesystem::path& path,
                               ProgressCallback progress = {},
                               const std::atomic<bool>* cancel_requested = nullptr,
                               RemoteCommandControl* command_control = nullptr);
    static std::string WithCacheBust(const std::string& url);

private:
    static void EnsureInitialized();
};

} // namespace textlt
