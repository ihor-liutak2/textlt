#pragma once

#include <atomic>
#include <chrono>
#include <optional>
#include <string_view>

#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

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

struct Error {
    TransferFailure code;
    std::string reason;
};

struct Cancelled {};

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
    void Post(std::string_view target, std::string body, Milliseconds timeout);
    void Upload(std::string_view target, std::istream& input, std::uint64_t length,
                Milliseconds timeout, const std::function<void(std::uint64_t)>& progress);
    void Check() const;
    void SetCancellation(const std::atomic<bool>* cancel) { cancel_ = cancel; }

private:
    template<class Initiate> boost::system::error_code Wait(
        Milliseconds timeout, Initiate initiate, TransferFailure code = TransferFailure::ConnectionFailure,
        std::string_view timeout_reason = "LocalSend request timed out");

    net::io_context io_;
    ssl::stream<beast::tcp_stream> stream_;
    beast::flat_buffer buffer_;
    const std::atomic<bool>& stop_;
    const std::atomic<bool>* cancel_;
};

} // namespace textlt::connect::localsend
