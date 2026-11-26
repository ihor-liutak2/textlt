#include "telegram_api.hpp"

#include <algorithm>
#include <random>
#include <string_view>

#include "nlohmann/json.hpp"
#include "remote/remote_command_runner.hpp"
#include "remote/remote_http_client.hpp"

namespace textlt::telegram {
namespace {

using Json = nlohmann::json;

class TelegramHttpApi final : public TelegramApi {
public:
    TelegramApiResponse GetMe(
        const std::string& token,
        const std::atomic<bool>* cancel_requested) override {
        return Request("GET", token, "getMe", {}, 15, cancel_requested);
    }

    TelegramApiResponse GetUpdates(
        const std::string& token,
        std::int64_t offset,
        int timeout_seconds,
        const std::atomic<bool>* cancel_requested) override {
        const int bounded_timeout = std::clamp(timeout_seconds, 1, 50);
        const std::string target = "getUpdates?offset=" + std::to_string(offset) +
            "&timeout=" + std::to_string(bounded_timeout) +
            "&allowed_updates=" + textlt::RemoteHttpClient::UrlEncode("[\"message\"]");
        return Request("GET", token, target, {}, bounded_timeout + 8, cancel_requested);
    }

    TelegramApiResponse SendMessage(
        const std::string& token,
        const std::string& chat_id,
        const std::string& text,
        const std::atomic<bool>* cancel_requested) override {
        const Json body = {{"chat_id", chat_id}, {"text", text}};
        return Request("POST", token, "sendMessage", body.dump(), 20, cancel_requested);
    }

    TelegramApiResponse SendDocument(
        const std::string& token, const std::string& chat_id, const std::string& filename,
        const std::string& bytes, const std::atomic<bool>* cancel_requested) override {
        std::random_device random;
        std::string boundary;
        do {
            boundary = "textlt-" + std::to_string(random()) + "-" + std::to_string(random());
        } while (bytes.find(boundary) != std::string::npos ||
                 chat_id.find(boundary) != std::string::npos ||
                 filename.find(boundary) != std::string::npos);

        std::string body;
        body.reserve(bytes.size() + filename.size() + chat_id.size() + 256);
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
            chat_id + "\r\n--" + boundary +
            "\r\nContent-Disposition: form-data; name=\"document\"; filename=\"" + filename +
            "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
        body += bytes;
        body += "\r\n--" + boundary + "--\r\n";
        const auto response = client_.RequestStreaming(
            "POST", "https://api.telegram.org/bot" + token + "/sendDocument",
            {"Accept: application/json", "Content-Type: multipart/form-data; boundary=" + boundary},
            body, 300, cancel_requested, {}, nullptr, false);
        return {response.status_code, response.body, response.error};
    }

    TelegramApiResponse GetFile(
        const std::string& token, const std::string& file_id,
        const std::atomic<bool>* cancel_requested) override {
        return Request("GET", token, "getFile?file_id=" + RemoteHttpClient::UrlEncode(file_id),
                       {}, 20, cancel_requested);
    }

    TelegramApiResponse DownloadFile(
        const std::string& token, const std::string& file_path,
        const std::atomic<bool>* cancel_requested, FileProgress progress) override {
        std::string path;
        std::string_view remainder = file_path;
        while (!remainder.empty()) {
            const auto slash = remainder.find('/');
            const auto segment = remainder.substr(0, slash);
            if (segment.empty() || segment == "." || segment == "..") {
                return {0, {}, "Telegram returned an invalid file path."};
            }
            if (!path.empty()) path += '/';
            path += RemoteHttpClient::UrlEncode(std::string(segment));
            if (slash == std::string_view::npos) break;
            remainder.remove_prefix(slash + 1);
        }
        if (path.empty() || file_path.back() == '/') {
            return {0, {}, "Telegram returned an invalid file path."};
        }

        RemoteCommandControl control;
        constexpr std::uint64_t trailer_reserve = 64;
        std::uint64_t received = 0;
        auto response = client_.RequestStreaming(
            "GET", "https://api.telegram.org/file/bot" + token + "/" + path, {}, {}, 30,
            cancel_requested, [&](const std::string& chunk) {
                received += chunk.size();
                // curl appends a status trailer to stdout.
                if (received > kTelegramDownloadLimit + trailer_reserve) control.RequestStop();
                else if (received > trailer_reserve) progress(received - trailer_reserve);
            }, &control, false);
        if (received > kTelegramDownloadLimit + trailer_reserve || response.body.size() > kTelegramDownloadLimit) {
            return {0, {}, "Telegram files over 20 MB cannot be downloaded."};
        }
        if (!response.ok) {
            return {response.status_code, {}, response.error.empty()
                ? "Telegram could not download the file (HTTP " +
                    std::to_string(response.status_code) + ")."
                : std::move(response.error)};
        }
        return {response.status_code, std::move(response.body), {}};
    }

private:
    TelegramApiResponse Request(
        const std::string& method,
        const std::string& token,
        const std::string& target,
        const std::string& body,
        int timeout_seconds,
        const std::atomic<bool>* cancel_requested) {
        if (token.empty()) return {0, {}, "Telegram bot token is empty."};

        const std::string url = "https://api.telegram.org/bot" + token + "/" + target;
        std::vector<std::string> headers{"Accept: application/json"};
        if (method == "POST") headers.push_back("Content-Type: application/json");
        const auto response = client_.RequestStreaming(
            method, url, headers, body, timeout_seconds, cancel_requested, {}, nullptr, false);
        return {response.status_code, response.body, response.error};
    }

    textlt::RemoteHttpClient client_;
};

} // namespace

std::shared_ptr<TelegramApi> MakeTelegramHttpApi() {
    return std::make_shared<TelegramHttpApi>();
}

} // namespace textlt::telegram
