#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace textlt::telegram {

inline constexpr std::uint64_t kTelegramUploadLimit = 50 * 1024 * 1024;
inline constexpr std::uint64_t kTelegramDownloadLimit = 20 * 1024 * 1024;

struct TelegramApiResponse {
    long http_status = 0;
    std::string body;
    std::string error;
};

class TelegramApi {
public:
    using FileProgress = std::function<void(std::uint64_t)>;
    virtual ~TelegramApi() = default;

    virtual TelegramApiResponse GetMe(
        const std::string& token,
        const std::atomic<bool>* cancel_requested = nullptr) = 0;
    virtual TelegramApiResponse GetUpdates(
        const std::string& token,
        std::int64_t offset,
        int timeout_seconds,
        const std::atomic<bool>* cancel_requested) = 0;
    virtual TelegramApiResponse SendMessage(
        const std::string& token,
        const std::string& chat_id,
        const std::string& text,
        const std::atomic<bool>* cancel_requested) = 0;
    virtual TelegramApiResponse SendDocument(
        const std::string& token, const std::string& chat_id, const std::string& filename,
        const std::string& bytes, const std::atomic<bool>* cancel_requested) = 0;
    virtual TelegramApiResponse GetFile(
        const std::string& token, const std::string& file_id,
        const std::atomic<bool>* cancel_requested) = 0;
    virtual TelegramApiResponse DownloadFile(
        const std::string& token, const std::string& file_path,
        const std::atomic<bool>* cancel_requested, FileProgress progress) = 0;
};

std::shared_ptr<TelegramApi> MakeTelegramHttpApi();

} // namespace textlt::telegram
