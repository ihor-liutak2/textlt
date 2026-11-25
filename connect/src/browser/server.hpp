#pragma once

#include <atomic>

#include "textlt/connect/textlt_provider.hpp"

namespace textlt::connect::detail {
class ProtocolWire;
}

namespace textlt::connect::browser {

class Server {
public:
    using Receive = std::function<void(detail::ProtocolWire&, const std::string&,
                                       const std::function<void()>&)>;

    Server(const TextLTBrowserOptions& options, std::chrono::milliseconds transfer_timeout,
           const std::atomic<bool>& stopping, const std::atomic<bool>& cancelled, Receive receive);
    ~Server();
    std::uint16_t Port() const;

private:
    struct Listener;
    std::unique_ptr<Listener> listener_;
};

} // namespace textlt::connect::browser
