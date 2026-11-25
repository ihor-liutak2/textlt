#include "../providers/localsend/https.hpp"
#include "../providers/textlt/protocol.hpp"

#include <boost/beast/websocket/ssl.hpp>

namespace textlt::connect::localsend {

struct Https::WebSocket {
    explicit WebSocket(ssl::stream<beast::tcp_stream>& tls) : stream(tls) {}
    beast::websocket::stream<ssl::stream<beast::tcp_stream>&> stream;
};

void Https::Upgrade(const Headers& request, Milliseconds timeout) {
    if (buffer_.size() || !beast::websocket::is_upgrade(request.get())) {
        throw Error{TransferFailure::InvalidProtocol, "invalid TextLT WebSocket upgrade"};
    }
    websocket_ = std::make_shared<WebSocket>(stream_);
    auto& socket = websocket_->stream;
    socket.binary(true);
    socket.read_message_max(detail::kMaxTextBytes);
    Wait(timeout, [&](auto done) { socket.async_accept(request.get(), done); },
         TransferFailure::ConnectionFailure, "TextLT WebSocket upgrade timed out");
}

std::string Https::ReadWebSocket(std::size_t count, Milliseconds timeout) {
    auto& socket = websocket_->stream;
    std::string data(count, '\0');
    const auto deadline = Clock::now() + timeout;
    for (std::size_t offset = 0; offset < count;) {
        std::size_t received = 0;
        Wait(std::chrono::ceil<Milliseconds>(deadline - Clock::now()), [&](auto done) {
            socket.async_read_some(net::buffer(data.data() + offset, count - offset),
                [&, done](boost::system::error_code error, std::size_t size) {
                    received = size;
                    done(error);
                });
        }, TransferFailure::ConnectionFailure, "TextLT browser transfer timed out");
        if (socket.got_text() || !received) {
            throw Error{TransferFailure::InvalidProtocol, "invalid TextLT WebSocket frame"};
        }
        offset += received;
    }
    return data;
}

void Https::WriteWebSocket(std::string_view data, Milliseconds timeout) {
    Wait(timeout, [&](auto done) { websocket_->stream.async_write(net::buffer(data.data(), data.size()), done); },
         TransferFailure::ConnectionFailure, "TextLT browser transfer timed out");
}

} // namespace textlt::connect::localsend
