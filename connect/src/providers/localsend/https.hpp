#pragma once

#include <atomic>
#include <chrono>
#include <optional>
#include <string_view>

#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

#include "errors.hpp"
#include "textlt/connect/localsend_provider.hpp"

namespace textlt::connect::localsend {

namespace net = boost::asio;
namespace ssl = net::ssl;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;
using Headers = http::request_parser<http::buffer_body>;

std::string RandomToken();
std::optional<std::string> NormalizeFingerprint(std::string_view fingerprint);
std::string Hex(const unsigned char* bytes, std::size_t size);
std::string LoadIdentity(ssl::context& context, const std::filesystem::path& path);

class Https {
public:
    Https(ssl::context& context, const std::atomic<bool>& stop, const std::atomic<bool>* cancel = nullptr);
    tcp::socket& Socket();
    std::string Connect(const tcp::endpoint& address, Milliseconds timeout);
    void Connect(const tcp::endpoint& address, const std::string& fingerprint, Milliseconds timeout);
    void Accept(Milliseconds timeout);
    std::optional<std::string> Fingerprint();
    bool Disconnected();
    void ReadHeaders(Headers& request, Milliseconds timeout);
    void ReadBody(Headers& request, Milliseconds timeout,
                  const std::function<void(const char*, std::size_t)>& consume);
    http::response<http::string_body> ReadResponse(Milliseconds timeout,
        std::string_view timeout_reason = "LocalSend request timed out");
    void Reply(http::status status, std::string body, Milliseconds timeout);
    void ReplyPage(std::string body, const char* content_type, const std::string& policy, Milliseconds timeout);
    void Upgrade(const Headers& request, Milliseconds timeout);
    std::string ReadWebSocket(std::size_t count, Milliseconds timeout);
    void WriteWebSocket(std::string_view data, Milliseconds timeout);
    void Post(std::string_view target, std::string body, Milliseconds timeout);
    void Upload(std::string_view target, std::istream& input, std::uint64_t length,
                Milliseconds timeout, const std::function<void(std::uint64_t)>& progress);
    void Check() const;
    void SetCancellation(const std::atomic<bool>* cancel) { cancel_ = cancel; }

private:
    template<class Initiate> boost::system::error_code Wait(
        Milliseconds timeout, Initiate initiate, TransferFailure code = TransferFailure::ConnectionFailure,
        std::string_view timeout_reason = "LocalSend request timed out");

    struct WebSocket;

    net::io_context io_;
    ssl::stream<beast::tcp_stream> stream_;
    beast::flat_buffer buffer_;
    std::shared_ptr<WebSocket> websocket_;
    const std::atomic<bool>& stop_;
    const std::atomic<bool>* cancel_;
};

template<class Initiate>
boost::system::error_code Https::Wait(Milliseconds timeout, Initiate initiate, TransferFailure code,
                                     std::string_view timeout_reason) {
    Check();
    bool done = false;
    boost::system::error_code result;
    const auto deadline = Clock::now() + timeout;
    io_.restart();
    initiate([&](boost::system::error_code error, auto...) {
        result = error;
        done = true;
    });
    while (!done && Clock::now() < deadline && !stop_ && (!cancel_ || !*cancel_)) {
        io_.run_for(std::chrono::milliseconds(20));
    }
    if (!done) {
        boost::system::error_code ignored;
        Socket().cancel(ignored);
        io_.restart();
        io_.run();
        Check();
        throw Error{TransferFailure::Timeout, std::string(timeout_reason)};
    }
    if (result && result != http::error::need_buffer && result != http::error::need_more) {
        throw Error{code, result.message()};
    }
    return result;
}

} // namespace textlt::connect::localsend
