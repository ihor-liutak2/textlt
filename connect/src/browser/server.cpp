#include "server.hpp"

#include "page.hpp"
#include "../providers/textlt/protocol.hpp"

#include <algorithm>
#include <future>
#include <thread>

namespace textlt::connect::browser {
namespace {

using namespace localsend;
constexpr auto kRequestTimeout = std::chrono::seconds(5);

class BrowserWire final : public detail::ProtocolWire {
public:
    BrowserWire(Https& client, Milliseconds timeout) : client_(client) { RenewDeadline(timeout); }

    void RenewDeadline(Milliseconds timeout) override { deadline_ = Clock::now() + timeout; }
    void CheckActive() const override { client_.Check(); }
    bool Disconnected() const override { return client_.Disconnected(); }
    void Write(std::string_view data) const override { client_.WriteWebSocket(data, Remaining()); }
    std::string Read(std::size_t size) const override { return client_.ReadWebSocket(size, Remaining()); }

private:
    bool SupportsVersion(std::uint8_t version) const override {
        return version == detail::kTextProtocolVersion || version == detail::kFileProtocolVersion;
    }

    Milliseconds Remaining() const {
        const auto remaining = std::chrono::ceil<Milliseconds>(deadline_ - Clock::now());
        if (remaining.count() <= 0) throw detail::IoError::Timeout;
        return remaining;
    }

    Https& client_;
    Clock::time_point deadline_;
};

} // namespace

struct Server::Listener {
    Listener(const TextLTBrowserOptions& options, Milliseconds transfer_timeout,
             const std::atomic<bool>& stopping, const std::atomic<bool>& cancelled, Receive receive)
        : theme_(options.theme), transfer_timeout_(transfer_timeout), stopping_(stopping),
          cancelled_(cancelled), receive_(std::move(receive)), tls_(ssl::context::tls_server), acceptor_(io_) {
        tls_.set_password_callback([](std::size_t, ssl::context::password_purpose) { return std::string{}; });
        tls_.use_certificate_chain_file(options.certificate_pem);
        tls_.use_private_key_file(options.private_key_pem, ssl::context::pem);
        if (SSL_CTX_check_private_key(tls_.native_handle()) != 1 ||
            SSL_CTX_set_min_proto_version(tls_.native_handle(), TLS1_2_VERSION) != 1) {
            throw std::runtime_error("could not configure the HTTPS certificate and key");
        }
        const auto address = net::ip::make_address_v4(options.listen_ip);
        if (address.is_unspecified() || address.is_multicast() || address == net::ip::address_v4::broadcast()) {
            throw std::runtime_error("the HTTPS listener requires a specific IPv4 address");
        }
        acceptor_.open(tcp::v4());
        acceptor_.bind(tcp::endpoint(address, 0));
        acceptor_.listen();
        acceptor_.non_blocking(true);
        port_ = acceptor_.local_endpoint().port();
        worker_ = std::thread([this] { Listen(); });
    }

    ~Listener() {
        if (worker_.joinable()) worker_.join();
    }

    void Listen() {
        constexpr std::size_t kMaxClients = 8;
        std::vector<std::future<void>> clients;
        clients.reserve(kMaxClients);
        while (!stopping_) {
            clients.erase(std::remove_if(clients.begin(), clients.end(), [](auto& client) {
                return client.wait_for(Milliseconds::zero()) == std::future_status::ready;
            }), clients.end());
            if (clients.size() < kMaxClients) {
                auto client = std::make_unique<Https>(tls_, stopping_);
                boost::system::error_code error;
                acceptor_.accept(client->Socket(), error);
                if (!error) {
                    try {
                        clients.push_back(std::async(std::launch::async, [this, client = std::move(client)] {
                            try {
                                Handle(*client);
                            } catch (const Cancelled&) {
                            } catch (const Error&) {
                            } catch (const std::exception&) {
                            }
                        }));
                    } catch (const std::system_error&) {
                        std::this_thread::sleep_for(Milliseconds(20));
                    }
                    continue;
                }
            }
            std::this_thread::sleep_for(Milliseconds(20));
        }
    }

    void Handle(Https& client) {
        client.Accept(kRequestTimeout);
        Headers request;
        client.ReadHeaders(request, kRequestTimeout);
        const auto raw = request.get().target();
        const std::string_view target(raw.data(), raw.size());
        const auto path = target.substr(0, target.find('?'));
        if (request.get().method() != http::verb::get) {
            client.Reply(http::status::method_not_allowed, {}, kRequestTimeout);
        } else if (path == "/transfer") {
            if (!SameOrigin(client, request)) {
                client.Reply(http::status::forbidden, {}, kRequestTimeout);
                return;
            }
            const auto peer = std::string(kTextLTBrowserPeerPrefix) + RandomToken();
            client.Upgrade(request, kRequestTimeout);
            BrowserWire wire(client, transfer_timeout_);
            receive_(wire, peer, [&] { client.SetCancellation(&cancelled_); });
        } else if (!ServePage(client, request, path, Protocol::TextLT, theme_, kRequestTimeout)) {
            client.Reply(http::status::not_found, {}, kRequestTimeout);
        }
    }

    BrowserThemeSource theme_;
    Milliseconds transfer_timeout_;
    const std::atomic<bool>& stopping_;
    const std::atomic<bool>& cancelled_;
    Receive receive_;
    ssl::context tls_;
    net::io_context io_;
    tcp::acceptor acceptor_;
    std::uint16_t port_ = 0;
    std::thread worker_;
};

Server::Server(const TextLTBrowserOptions& options, Milliseconds transfer_timeout,
               const std::atomic<bool>& stopping, const std::atomic<bool>& cancelled, Receive receive)
    : listener_(std::make_unique<Listener>(options, transfer_timeout, stopping, cancelled, std::move(receive))) {}

Server::~Server() = default;

std::uint16_t Server::Port() const { return listener_->port_; }

} // namespace textlt::connect::browser
