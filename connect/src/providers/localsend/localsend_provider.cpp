#include "https.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

#include <boost/asio/ip/multicast.hpp>
#include <boost/asio/ip/udp.hpp>
#include <openssl/evp.h>
#include <sstream>

#include "nlohmann/json.hpp"
#include "../textlt/protocol.hpp"

namespace textlt::connect {
namespace localsend {
namespace {

using Json = nlohmann::json;
using Udp = net::ip::udp;
using Status = http::status;
constexpr char kRegister[] = "/api/localsend/v2/register";
constexpr char kPrepare[] = "/api/localsend/v2/prepare-upload";
constexpr char kUpload[] = "/api/localsend/v2/upload";
constexpr char kCancel[] = "/api/localsend/v2/cancel";
constexpr std::size_t kMaxControlBytes = 1024 * 1024;
constexpr std::size_t kMaxFiles = 128;
constexpr std::size_t kMaxPeers = 256;
constexpr std::size_t kMaxClients = 16;
constexpr auto kPeerLifetime = std::chrono::seconds(70);
constexpr auto kAnnounceInterval = std::chrono::seconds(20);
constexpr auto kUnicastInterval = std::chrono::seconds(40);
constexpr auto kUnicastTimeout = std::chrono::milliseconds(350);
constexpr std::size_t kUnicastWorkers = 8;
constexpr auto kPoll = std::chrono::milliseconds(20);

std::optional<tcp::endpoint> ParseEndpoint(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    value = value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    const auto separator = value.find(':');
    boost::system::error_code error;
    const auto address = net::ip::make_address_v4(std::string(value.substr(0, separator)), error);
    if (error || address.is_unspecified() || address.is_multicast() ||
        address == net::ip::address_v4::broadcast()) return {};
    std::uint16_t port = 53317;
    if (separator != std::string_view::npos) {
        const auto digits = value.substr(separator + 1);
        const auto end = digits.data() + digits.size();
        const auto result = std::from_chars(digits.data(), end, port);
        if (result.ec != std::errc{} || result.ptr != end || port == 0) return {};
    }
    return tcp::endpoint(address, port);
}

std::vector<Capability> TextAndFiles() { return {{kTextTypeId, {}}, {kFileTypeId, {}}}; }

enum class PeerRoute { Incoming, Multicast, Verified, Manual };

struct PeerRecord {
    Peer peer;
    tcp::endpoint address;
    Clock::time_point expires;
    Clock::time_point next_reply{};
    PeerRoute route = PeerRoute::Incoming;
};

bool CompatibleVersion(const Json& json) {
    if (!json.is_object() || json.value("protocol", "") != "https") return false;
    const auto version = json.value("version", std::string{});
    return version.size() >= 3 && version.compare(0, 2, "2.") == 0 &&
        std::all_of(version.begin() + 2, version.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::optional<PeerRecord> ParsePeer(const Json& json, const net::ip::address& ip) {
    if (!CompatibleVersion(json) || !ip.is_v4()) return {};
    auto alias = json.value("alias", std::string{});
    auto fingerprint = NormalizeFingerprint(json.value("fingerprint", std::string{}));
    const auto found = json.find("port");
    if (!fingerprint || found == json.end() || !found->is_number_integer() ||
        *found < 1 || *found > 65535) return {};
    const auto port = found->get<std::uint16_t>();
    alias.erase(std::remove_if(alias.begin(), alias.end(), [](unsigned char ch) {
        return ch < 32 || ch == 127;
    }), alias.end());
    if (alias.empty() || alias.size() > 255) alias = "LocalSend peer";
    return PeerRecord{{{kLocalSendProviderId, *fingerprint}, std::move(alias), true, TextAndFiles(), 2},
                      tcp::endpoint(ip, port), Clock::now() + kPeerLifetime};
}

Json Parse(std::string_view body) {
    return Json::parse(body.begin(), body.end(), [](int depth, Json::parse_event_t, Json&) {
        if (depth > 12) throw Error{TransferFailure::InvalidProtocol, "LocalSend JSON nesting limit exceeded"};
        return true;
    });
}

std::string Encode(std::string_view value) {
    std::string out;
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else out += "%" + Hex(&c, 1);
    }
    return out;
}

int Digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

std::optional<std::string> Decode(std::string_view value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') {
            result += value[i] == '+' ? ' ' : value[i];
        } else {
            if (i + 2 >= value.size()) return {};
            const int high = Digit(value[i + 1]), low = Digit(value[i + 2]);
            if (high < 0 || low < 0) return {};
            result += static_cast<char>(high * 16 + low);
            i += 2;
        }
    }
    return result;
}

std::map<std::string, std::string> Query(std::string_view query) {
    std::map<std::string, std::string> values;
    while (!query.empty()) {
        const auto end = query.find('&');
        const auto field = query.substr(0, end);
        const auto equals = field.find('=');
        if (equals == std::string_view::npos) return {};
        const auto key = Decode(field.substr(0, equals));
        const auto value = Decode(field.substr(equals + 1));
        if (!key || !value || !values.emplace(*key, *value).second) return {};
        if (end == std::string_view::npos) break;
        query.remove_prefix(end + 1);
    }
    return values;
}

bool Same(const TransferIdentity& a, const TransferIdentity& b) {
    return a.peer.provider_id == b.peer.provider_id && a.peer.peer_id == b.peer.peer_id &&
           a.transfer_id == b.transfer_id;
}

struct Outcome {
    TransferState state;
    std::string reason{};
    std::optional<TransferFailure> failure{};
};

Outcome Failed(TransferFailure failure, std::string reason) {
    return {TransferState::Failed, std::move(reason), failure};
}

Outcome ResponseFailure(Status status, bool preparing) {
    switch (status) {
    case Status::unauthorized: return Failed(TransferFailure::PinRequired, "LocalSend PIN required or invalid");
    case Status::forbidden:
        return preparing ? Outcome{TransferState::Declined, "LocalSend offer rejected"}
                         : Failed(TransferFailure::PeerAuthenticationFailed, "LocalSend upload authorization rejected");
    case Status::conflict: return Failed(TransferFailure::Busy, "LocalSend receiver is busy");
    case Status::too_many_requests: return Failed(TransferFailure::RateLimited, "LocalSend receiver rate limited the request");
    case Status::unprocessable_entity: return Failed(TransferFailure::ChecksumMismatch, "LocalSend upload checksum mismatch");
    default:
        return Failed(TransferFailure::InvalidProtocol,
                      "LocalSend responded with HTTP " + std::to_string(static_cast<unsigned>(status)));
    }
}

enum class ItemState { Unoffered, Waiting, Accepted, Uploading, Finished };

struct Item {
    std::string wire_id;
    IncomingOffer offer;
    std::optional<std::string> preview;
    std::optional<std::string> checksum;
    std::string token;
    FileDestinationFactory destination;
    ItemState state = ItemState::Unoffered;
};

struct Session {
    std::string id;
    net::ip::address address;
    std::optional<std::string> fingerprint;
    std::vector<Item> items;
    std::atomic<bool> cancelled{false};
    bool preparing = true;
    Clock::time_point expires;
};

struct Outgoing {
    OutgoingTransfer transfer;
    tcp::endpoint address;
    std::atomic<bool> cancelled{false};
    bool awaiting_pin = false;
    std::optional<std::string> pin;
};

class LocalSendProvider final : public ILocalSendProvider {
public:
    explicit LocalSendProvider(LocalSendOptions options) : options_(std::move(options)) {}
    ~LocalSendProvider() override { Stop(); }

    std::string Id() const override { return kLocalSendProviderId; }
    std::vector<Capability> Capabilities() const override { return TextAndFiles(); }

    ProviderResult Start(ProviderEventSink sink) override {
        if (!stopping_) return {false, "LocalSend is already running"};
        LocalSendRuntime runtime;
        runtime.interface_ip = options_.interface_ip;
        if (!sink || !options_.discovery_port || options_.alias.empty() || options_.alias.size() > 255 ||
            !detail::ValidUtf8(options_.alias) ||
            options_.connect_timeout.count() <= 0 || options_.decision_timeout.count() <= 0 ||
            options_.inactivity_timeout.count() <= 0) {
            return StartFailure(runtime, "invalid LocalSend settings");
        }

        try {
            const auto address = net::ip::make_address_v4(options_.listen_ip);
            const auto interface_address = net::ip::make_address_v4(options_.interface_ip);
            const auto multicast = net::ip::make_address_v4(options_.multicast_address);
            if (!multicast.is_multicast()) {
                return StartFailure(runtime, "LocalSend discovery address must be IPv4 multicast");
            }
            context_ = std::make_unique<ssl::context>(ssl::context::tls);
            fingerprint_ = LoadIdentity(*context_, options_.identity_file);
            acceptor_.open(tcp::v4());
#ifndef _WIN32
            acceptor_.set_option(net::socket_base::reuse_address(true));
#else
            const BOOL exclusive = TRUE;
            if (::setsockopt(acceptor_.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                             reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) != 0) {
                throw Error{TransferFailure::ConnectionFailure, "could not set exclusive LocalSend HTTPS binding"};
            }
#endif
            boost::system::error_code bind_error;
            runtime.https_bind = LocalSendCheck::Failed;
            acceptor_.bind(tcp::endpoint(address, options_.receive_port), bind_error);
            if (bind_error) throw Error{TransferFailure::ConnectionFailure,
                "LocalSend HTTPS port " + std::to_string(options_.receive_port) + ": " + bind_error.message()};
            runtime.https_bind = LocalSendCheck::Succeeded;
            acceptor_.listen();
            acceptor_.non_blocking(true);
            receive_port_ = acceptor_.local_endpoint().port();
            runtime.https_port = receive_port_;

            discovery_.open(Udp::v4());
            discovery_.set_option(net::socket_base::reuse_address(true));
            runtime.udp_bind = LocalSendCheck::Failed;
            discovery_.bind(Udp::endpoint(Udp::v4(), options_.discovery_port), bind_error);
            if (bind_error) throw Error{TransferFailure::ConnectionFailure,
                "LocalSend UDP port " + std::to_string(options_.discovery_port) + ": " + bind_error.message()};
            runtime.udp_bind = LocalSendCheck::Succeeded;
            runtime.udp_port = discovery_.local_endpoint().port();
            runtime.multicast_join = LocalSendCheck::Failed;
            discovery_.set_option(net::ip::multicast::join_group(multicast, interface_address));
            runtime.multicast_join = LocalSendCheck::Succeeded;
            if (!interface_address.is_unspecified()) {
                discovery_.set_option(net::ip::multicast::outbound_interface(interface_address));
            }
            discovery_.set_option(net::ip::multicast::enable_loopback(true));
            discovery_.set_option(net::ip::multicast::hops(1));
            discovery_.non_blocking(true);
            group_ = Udp::endpoint(multicast, options_.discovery_port);

            sink_ = std::move(sink);
            stopping_ = false;
            server_ = std::thread([this] { Listen(); });
            browser_ = std::thread([this] { Discover(); });
            if (options_.unicast_discovery_subnet) {
                unicast_browser_ = std::thread([this] { DiscoverUnicast(); });
            }
            sender_ = std::thread([this] { SendLoop(); });
            runtime.listening = true;
            ReportRuntime(runtime);
            return {true, {}};
        } catch (const Error& error) {
            return StartFailure(runtime, error.reason);
        } catch (const std::exception& error) {
            return StartFailure(runtime, std::string("LocalSend could not start: ") + error.what());
        }
    }

    void Stop() noexcept override {
        bool was_running;
        {
            std::lock_guard lock(mutex_);
            was_running = !stopping_.exchange(true);
        }
        changed_.notify_all();
        if (browser_.joinable()) browser_.join();
        if (unicast_browser_.joinable()) unicast_browser_.join();
        if (server_.joinable()) server_.join();
        if (sender_.joinable()) sender_.join();
        boost::system::error_code ignored;
        acceptor_.close(ignored);
        discovery_.close(ignored);
        session_.reset();
        outgoing_.reset();
        manual_endpoint_.reset();
        refresh_requested_ = false;
        unicast_refresh_requested_ = false;
        peers_.clear();
        discovery_status_ = {};
        context_.reset();
        sink_ = {};
        if (was_running) ReportRuntime({});
    }

    ProviderResult ConnectByIp(std::string_view address) override {
        const auto endpoint = ParseEndpoint(address);
        if (!endpoint) return {false, "Enter a peer IPv4 address or IPv4:port (default port 53317)"};
        std::lock_guard lock(mutex_);
        if (stopping_) return {false, "Enable LocalSend and start networking first"};
        if (manual_endpoint_) return {false, "A LocalSend connection attempt is already running"};
        manual_endpoint_ = *endpoint;
        return {true, {}};
    }

    ProviderResult RefreshPeers() override {
        std::lock_guard lock(mutex_);
        if (stopping_) return {false, "Start LocalSend to refresh peers"};
        discovery_status_.multicast_send = LocalSendCheck::NotAttempted;
        discovery_status_.multicast_error.clear();
        refresh_requested_ = true;
        unicast_refresh_requested_ = true;
        changed_.notify_all();
        return {true, {}};
    }

    LocalSendDiscoveryStatus DiscoveryStatus() const override {
        std::lock_guard lock(mutex_);
        return discovery_status_;
    }

    std::vector<Peer> OnlinePeers() const override {
        std::lock_guard lock(mutex_);
        std::vector<Peer> online;
        for (const auto& [_, peer] : peers_) {
            if (peer.peer.online) online.push_back(peer.peer);
        }
        return online;
    }

    ProviderResult Send(OutgoingTransfer transfer) override {
        auto& metadata = transfer.metadata;
        if (transfer.identity.peer.provider_id != kLocalSendProviderId || metadata.schema_version) {
            return {false, "LocalSend accepts ordinary text and files only"};
        }
        if (const auto* text = std::get_if<TextPayload>(&transfer.payload)) {
            if (metadata.type_id != kTextTypeId || !detail::ValidUtf8(text->utf8)) {
                return {false, "LocalSend requires valid UTF-8 text"};
            }
            if (text->utf8.size() > kLocalSendInlineTextLimit) {
                return {false, "Text exceeds 16384 bytes. Send it as a .txt file."};
            }
            metadata.byte_length = text->utf8.size();
        } else {
            const auto* stream = std::get_if<std::unique_ptr<std::istream>>(&transfer.payload);
            if (metadata.type_id != kFileTypeId || !stream || !*stream || !metadata.byte_length ||
                !metadata.display_name || !detail::ValidFileName(*metadata.display_name)) {
                return {false, "invalid LocalSend file name, length, or stream"};
            }
        }
        std::lock_guard lock(mutex_);
        if (stopping_ || outgoing_) return {false, "LocalSend is stopped or sending another item"};
        const auto found = peers_.find(transfer.identity.peer.peer_id);
        if (found == peers_.end() ||
            (found->second.route != PeerRoute::Manual && found->second.expires <= Clock::now())) {
            return {false, "LocalSend peer is offline or unverified"};
        }
        outgoing_ = std::make_shared<Outgoing>();
        outgoing_->address = found->second.address;
        outgoing_->transfer = std::move(transfer);
        changed_.notify_all();
        return {true, {}};
    }

    ProviderResult Accept(const TransferIdentity& offer, std::unique_ptr<std::ostream> destination) override {
        if (destination) return {false, "use a staged destination for LocalSend files"};
        return Decide(offer, true, {});
    }

    ProviderResult AcceptFile(const TransferIdentity& offer, FileDestinationFactory destination) override {
        if (!destination) return {false, "LocalSend files require a staged destination"};
        return Decide(offer, true, std::move(destination));
    }

    ProviderResult Decline(const TransferIdentity& offer, std::string) override {
        return Decide(offer, false, {});
    }

    ProviderResult Cancel(const TransferIdentity& transfer, TransferDirection direction) override {
        std::lock_guard lock(mutex_);
        if (direction == TransferDirection::Outgoing && outgoing_ &&
            Same(outgoing_->transfer.identity, transfer)) {
            outgoing_->cancelled = true;
        } else if (direction == TransferDirection::Incoming && session_ &&
                   std::any_of(session_->items.begin(), session_->items.end(), [&](const Item& item) {
                       return Same(item.offer.identity, transfer) && item.state != ItemState::Finished;
                   })) {
            session_->cancelled = true;
        } else {
            return {false, "LocalSend transfer is no longer active"};
        }
        changed_.notify_all();
        return {true, {}};
    }

    ProviderResult RetryWithPin(const TransferIdentity& transfer, std::string pin) override {
        std::lock_guard lock(mutex_);
        if (!outgoing_ || !Same(outgoing_->transfer.identity, transfer) || !outgoing_->awaiting_pin) {
            return {false, "LocalSend is not waiting for a PIN"};
        }
        if (pin.empty() || pin.size() > 64) return {false, "enter a LocalSend PIN"};
        outgoing_->pin = std::move(pin);
        outgoing_->awaiting_pin = false;
        changed_.notify_all();
        return {true, {}};
    }

private:
    Json Info() const {
        return {{"alias", options_.alias}, {"version", "2.2"}, {"deviceType", "desktop"},
                {"fingerprint", fingerprint_}, {"port", receive_port_}, {"protocol", "https"},
                {"download", false}};
    }

    void Report(const TransferIdentity& id, TransferDirection direction, std::uint64_t total, Outcome outcome,
                std::optional<std::uint64_t> progress = {}) {
        if (outcome.state == TransferState::Completed) progress = total;
        sink_(TransferEvent{id, direction, outcome.state, progress, total,
            outcome.reason.empty() ? std::nullopt : std::optional<std::string>{std::move(outcome.reason)},
            outcome.failure});
    }

    std::optional<bool> Remember(PeerRecord peer, bool announce) {
        if (peer.peer.identity.peer_id == fingerprint_) return {};
        bool changed = false;
        bool reply = false;
        const auto now = Clock::now();
        {
            std::lock_guard lock(mutex_);
            auto found = peers_.find(peer.peer.identity.peer_id);
            if (found == peers_.end() && peers_.size() >= kMaxPeers) return {};
            if (found != peers_.end()) {
                auto& known = found->second;
                if (known.route == PeerRoute::Manual && peer.route != PeerRoute::Manual) return false;
                peer.next_reply = known.next_reply;
                if (known.route > peer.route && known.expires > now) {
                    if (known.address == peer.address) {
                        peer.route = known.route;
                    } else {
                        // A port forward can replace the peer's source address.
                        // Keep the better route while checking new multicast announcements.
                        reply = announce && known.next_reply <= now;
                        if (reply) known.next_reply = now + std::chrono::seconds(5);
                        return reply;
                    }
                }
            }
            changed = found == peers_.end() || found->second.peer.display_name != peer.peer.display_name ||
                found->second.address != peer.address;
            reply = announce && peer.next_reply <= now;
            if (reply) peer.next_reply = now + std::chrono::seconds(5);
            peers_.insert_or_assign(peer.peer.identity.peer_id, peer);
        }
        if (changed) sink_(peer.peer);
        return reply;
    }

    void Announce(bool initial) {
        auto message = Info();
        message["announce"] = initial;
        const auto bytes = message.dump();
        boost::system::error_code error;
        discovery_.send_to(net::buffer(bytes), group_, 0, error);
        if (!initial) return;
        {
            std::lock_guard lock(mutex_);
            discovery_status_.multicast_send = error ? LocalSendCheck::Failed : LocalSendCheck::Succeeded;
            discovery_status_.multicast_error = error ? error.message() : std::string{};
        }
        ReportDiscoveryChange();
    }

    void ReportDiscoveryChange() const noexcept {
        if (!options_.report_discovery_change) return;
        try { options_.report_discovery_change(); } catch (...) {}
    }

    void ReportRuntime(const LocalSendRuntime& runtime) const noexcept {
        if (!options_.report_runtime) return;
        try { options_.report_runtime(runtime); } catch (...) {}
    }

    ProviderResult StartFailure(LocalSendRuntime& runtime, std::string reason) {
        Stop();
        runtime.udp_port.reset();
        runtime.https_port.reset();
        runtime.listening = false;
        runtime.error = reason;
        ReportRuntime(runtime);
        return {false, std::move(reason)};
    }

    void ReportVerifiedPeer(const net::ip::address& address) const {
        if (!options_.report_verified_peer || address.is_loopback()) return;
        const auto ip = address.to_string();
        if (ip == options_.interface_ip ||
            std::find(options_.local_addresses.begin(), options_.local_addresses.end(), ip) !=
                options_.local_addresses.end()) return;
        try { options_.report_verified_peer(); } catch (...) {}
    }

    void ReportIncomingPeer(const net::ip::address& address, const Json& info) const {
        ReportVerifiedPeer(address);
        if (options_.report_incoming_mobile_peer && !address.is_loopback() &&
            info.contains("deviceType") && info["deviceType"] == "mobile") {
            try { options_.report_incoming_mobile_peer(); } catch (...) {}
        }
    }

    void Register(PeerRecord peer) {
        try {
            Https request(*context_, stopping_);
            request.Connect(peer.address, peer.peer.identity.peer_id, options_.connect_timeout);
            request.Post(kRegister, Info().dump(), options_.connect_timeout);
            const auto response = request.ReadResponse(options_.connect_timeout);
            if (response.result() == Status::ok) {
                ReportVerifiedPeer(peer.address.address());
                peer.route = PeerRoute::Verified;
                Remember(std::move(peer), false);
                return;
            }
        } catch (const Error&) {
        } catch (const Cancelled&) {
            return;
        } catch (const std::exception&) {
        }
        if (!stopping_) Announce(false);
    }

    void VerifyRegistration(PeerRecord peer) {
        if (peer.peer.identity.peer_id == fingerprint_) return;
        try {
            Https request(*context_, stopping_);
            request.Connect(peer.address, peer.peer.identity.peer_id, options_.connect_timeout);
            ReportVerifiedPeer(peer.address.address());
            peer.route = PeerRoute::Verified;
            Remember(std::move(peer), false);
        } catch (const Error&) {
            // The advertised listener did not prove its identity.
        }
    }

    LocalSendConnectResult RegisterAt(const tcp::endpoint& endpoint, PeerRoute route, Milliseconds timeout) {
        try {
            Https request(*context_, stopping_);
            const auto fingerprint = request.Connect(endpoint, timeout);
            if (fingerprint == fingerprint_) return {{}, "Enter another device's LocalSend address"};
            request.Post(kRegister, Info().dump(), timeout);
            const auto response = request.ReadResponse(timeout);
            if (response.result() != Status::ok) {
                return {{}, "LocalSend registration returned HTTP " + std::to_string(response.result_int())};
            }
            auto info = Parse(response.body());
            if (!info.is_object()) return {{}, "Invalid LocalSend registration response"};
            // The response can omit these fields. Use the endpoint that completed TLS.
            info["protocol"] = "https";
            info["port"] = endpoint.port();
            auto peer = ParsePeer(info, endpoint.address());
            if (!peer) return {{}, "Invalid LocalSend registration response"};
            if (peer->peer.identity.peer_id != fingerprint) {
                return {{}, "LocalSend peer certificate fingerprint mismatch"};
            }
            peer->route = route;
            const auto identity = peer->peer.identity;
            const auto name = peer->peer.display_name;
            if (!Remember(std::move(*peer), false).has_value()) return {{}, "LocalSend peer list is full"};
            ReportVerifiedPeer(endpoint.address());
            return {identity, "Connected to " + name};
        } catch (const Error& error) {
            return {{}, "LocalSend connection failed: " + error.reason};
        } catch (const std::exception&) {
            return {{}, "Invalid LocalSend registration response"};
        }
    }

    void ProbeSubnet(const LocalSendSubnet& subnet) {
        boost::system::error_code error;
        const auto address = net::ip::make_address_v4(subnet.address, error);
        if (error || address.is_unspecified() || address.is_multicast() ||
            address == net::ip::address_v4::broadcast() ||
            subnet.prefix_length < 1 || subnet.prefix_length > 31) return;

        // Bound each pass to the part of the subnet sharing the host's first 24 bits.
        const auto host = static_cast<std::uint32_t>(address.to_uint());
        const auto mask = std::uint32_t{0xffffffff} << (32 - subnet.prefix_length);
        const auto network = host & mask;
        const auto broadcast = network | ~mask;
        const auto block = host & std::uint32_t{0xffffff00};
        const bool has_broadcast = subnet.prefix_length < 31;
        const auto first = std::max(block, network + has_broadcast);
        const auto last = std::min(block | std::uint32_t{0xff}, broadcast - has_broadcast);
        const auto timeout = std::min(kUnicastTimeout, options_.connect_timeout);

        {
            std::lock_guard lock(mutex_);
            discovery_status_.unicast_subnet = subnet.address + "/" + std::to_string(subnet.prefix_length);
            discovery_status_.unicast_running = true;
        }
        ReportDiscoveryChange();
        std::atomic<std::uint64_t> next{first};
        std::atomic<unsigned> attempts{0}, registered{0};
        const auto probe = [&] {
            try {
                while (!stopping_) {
                    const auto candidate = next.fetch_add(1);
                    if (candidate > last) return;
                    if (candidate == host) continue;
                    const auto ip = net::ip::address_v4(static_cast<std::uint32_t>(candidate));
                    ++attempts;
                    const auto result = RegisterAt(tcp::endpoint(ip, options_.discovery_port),
                                                   PeerRoute::Verified, timeout);
                    if (result.peer) ++registered;
                }
            } catch (const Cancelled&) {
            }
        };
        std::vector<std::future<void>> workers;
        for (std::size_t i = 1; i < kUnicastWorkers && i <= last - first; ++i) {
            try {
                workers.push_back(std::async(std::launch::async, probe));
            } catch (const std::system_error&) {
                break;
            }
        }
        probe();
        for (auto& worker : workers) worker.wait();
        {
            std::lock_guard lock(mutex_);
            discovery_status_.unicast_running = false;
            discovery_status_.unicast_attempts = attempts;
            discovery_status_.unicast_registered = registered;
        }
        ReportDiscoveryChange();
    }

    void DiscoverUnicast() {
        while (!stopping_) {
            const auto subnet = options_.unicast_discovery_subnet();
            if (subnet) {
                ProbeSubnet(*subnet);
            } else {
                bool changed;
                {
                    std::lock_guard lock(mutex_);
                    changed = !discovery_status_.unicast_subnet.empty();
                    discovery_status_.unicast_subnet.clear();
                    discovery_status_.unicast_attempts = 0;
                    discovery_status_.unicast_registered = 0;
                }
                if (changed) ReportDiscoveryChange();
            }
            const auto interval = subnet ? kUnicastInterval : std::chrono::seconds(1);
            std::unique_lock lock(mutex_);
            changed_.wait_for(lock, interval, [this] { return stopping_ || unicast_refresh_requested_; });
            unicast_refresh_requested_ = false;
        }
    }

    void Discover() {
        std::array<char, 8192> bytes;
        auto next = Clock::now();
        while (!stopping_) {
            std::optional<tcp::endpoint> endpoint;
            {
                std::lock_guard lock(mutex_);
                endpoint = manual_endpoint_;
            }
            if (endpoint) {
                try {
                    auto result = RegisterAt(*endpoint, PeerRoute::Manual, options_.connect_timeout);
                    {
                        std::lock_guard lock(mutex_);
                        manual_endpoint_.reset();
                    }
                    if (!stopping_) sink_(std::move(result));
                } catch (const Cancelled&) {
                    return;
                }
            }
            const auto now = Clock::now();
            if (refresh_requested_.exchange(false) || now >= next) {
                Announce(true);
                next = now + kAnnounceInterval;
            }
            Udp::endpoint source;
            boost::system::error_code error;
            const auto size = discovery_.receive_from(net::buffer(bytes), source, 0, error);
            if (!error && size) {
                try {
                    const auto json = Parse({bytes.data(), size});
                    auto peer = ParsePeer(json, source.address());
                    if (peer && peer->peer.identity.peer_id != fingerprint_) {
                        {
                            std::lock_guard lock(mutex_);
                            ++discovery_status_.multicast_packets;
                        }
                        ReportDiscoveryChange();
                        peer->route = PeerRoute::Multicast;
                        if (Remember(*peer, json.value("announce", false)).value_or(false)) Register(std::move(*peer));
                    }
                } catch (const Error&) {
                } catch (const std::exception&) {
                }
            }
            std::vector<Peer> expired;
            {
                std::lock_guard lock(mutex_);
                for (auto it = peers_.begin(); it != peers_.end();) {
                    if (it->second.route == PeerRoute::Manual || it->second.expires > now) {
                        ++it;
                        continue;
                    }
                    it->second.peer.online = false;
                    expired.push_back(std::move(it->second.peer));
                    it = peers_.erase(it);
                }
            }
            for (auto& peer : expired) sink_(std::move(peer));
            if (error || !size) std::this_thread::sleep_for(kPoll);
        }
    }

    ProviderResult Decide(const TransferIdentity& offer, bool allow, FileDestinationFactory destination) {
        std::lock_guard lock(mutex_);
        if (!session_ || session_->cancelled || stopping_) return {false, "LocalSend offer has expired"};
        for (auto& item : session_->items) {
            if (!Same(item.offer.identity, offer) || item.state != ItemState::Waiting) continue;
            if (allow && static_cast<bool>(destination) == static_cast<bool>(item.preview)) {
                return {false, "wrong destination for LocalSend offer"};
            }
            item.destination = std::move(destination);
            item.state = allow ? ItemState::Accepted : ItemState::Finished;
            changed_.notify_all();
            return {true, {}};
        }
        return {false, "LocalSend offer is no longer pending"};
    }

    void FinishItem(const std::shared_ptr<Session>& session, Item& item, Outcome outcome) {
        {
            std::lock_guard lock(mutex_);
            item.state = ItemState::Finished;
            item.destination = {};
            if (session_ == session && !session->preparing &&
                std::all_of(session->items.begin(), session->items.end(), [](const Item& entry) {
                    return entry.state == ItemState::Finished;
                })) session_.reset();
        }
        Report(item.offer.identity, TransferDirection::Incoming, *item.offer.metadata.byte_length,
               std::move(outcome));
    }

    void AbortSession(const std::shared_ptr<Session>& session, Outcome outcome) {
        std::vector<const IncomingOffer*> reports;
        {
            std::lock_guard lock(mutex_);
            session->cancelled = true;
            if (session_ == session) session_.reset();
            for (auto& item : session->items) {
                if (item.state == ItemState::Accepted || item.state == ItemState::Waiting) {
                    reports.push_back(&item.offer);
                    item.state = ItemState::Finished;
                    item.destination = {};
                }
            }
        }
        changed_.notify_all();
        for (const auto* offer : reports) {
            Report(offer->identity, TransferDirection::Incoming, *offer->metadata.byte_length, outcome);
        }
    }

    void ExpireSession() {
        std::shared_ptr<Session> expired;
        bool cancelled = false;
        {
            std::lock_guard lock(mutex_);
            if (session_ && !session_->preparing && (session_->cancelled ||
                (session_->expires <= Clock::now() &&
                 std::none_of(session_->items.begin(), session_->items.end(),
                     [](const Item& item) { return item.state == ItemState::Uploading; })))) {
                expired = session_;
                cancelled = expired->cancelled;
            }
        }
        if (expired) AbortSession(expired, cancelled ? Outcome{TransferState::Cancelled}
            : Failed(TransferFailure::Timeout, "LocalSend upload did not finish in time"));
    }

    void Listen() {
        struct Client {
            std::shared_ptr<std::atomic<bool>> done;
            std::thread worker;
        };
        std::vector<Client> clients;
        clients.reserve(kMaxClients);
        while (!stopping_) {
            ExpireSession();
            for (auto it = clients.begin(); it != clients.end();) {
                if (*it->done) {
                    it->worker.join();
                    it = clients.erase(it);
                } else ++it;
            }
            if (clients.size() < kMaxClients) {
                auto connection = std::make_unique<Https>(*context_, stopping_);
                boost::system::error_code error;
                acceptor_.accept(connection->Socket(), error);
                if (!error) {
                    auto done = std::make_shared<std::atomic<bool>>(false);
                    try {
                        clients.push_back({done, std::thread([this, client = std::move(connection), done] {
                            Handle(*client);
                            *done = true;
                        })});
                    } catch (const std::system_error&) {
                        std::this_thread::sleep_for(kPoll);
                    }
                    continue;
                }
            }
            std::this_thread::sleep_for(kPoll);
        }
        for (auto& client : clients) client.worker.join();
        std::shared_ptr<Session> last;
        {
            std::lock_guard lock(mutex_);
            last = session_;
        }
        if (last) AbortSession(last, {TransferState::Cancelled});
    }

    void Handle(Https& client) {
        try {
            const auto address = client.Socket().remote_endpoint().address();
            client.Accept(options_.connect_timeout);
            const auto fingerprint = client.Fingerprint();
            Headers request;
            client.ReadHeaders(request, options_.connect_timeout);
            const auto target = request.get().target();
            const std::string_view url(target.data(), target.size());
            const auto marker = url.find('?');
            const auto path = url.substr(0, marker);
            if (request.get().method() != http::verb::post) {
                client.Reply(Status::method_not_allowed, {}, options_.connect_timeout);
                return;
            }
            if (path == kRegister || path == kPrepare) {
                std::string body;
                client.ReadBody(request, options_.connect_timeout, [&](const char* bytes, std::size_t count) {
                    if (count > kMaxControlBytes - body.size()) {
                        throw Error{TransferFailure::InvalidProtocol, "LocalSend metadata is too large"};
                    }
                    body.append(bytes, count);
                });
                const auto json = Parse(body);
                if (path == kRegister) {
                    auto peer = ParsePeer(json, address);
                    if (fingerprint && (!peer || fingerprint != peer->peer.identity.peer_id)) {
                        client.Reply(Status::forbidden, {}, options_.connect_timeout);
                    } else {
                        if (fingerprint && peer) {
                            Remember(*peer, false);
                            ReportIncomingPeer(address, json);
                        }
                        client.Reply(Status::ok, Info().dump(), options_.connect_timeout);
                        // Some LocalSend clients register without presenting a client certificate.
                        // Check the certificate on their receive port before adding them.
                        if (!fingerprint && peer) VerifyRegistration(std::move(*peer));
                    }
                } else Prepare(client, json, address, fingerprint);
                return;
            }
            const auto parameters = marker == std::string_view::npos ? std::map<std::string, std::string>{}
                : Query(url.substr(marker + 1));
            if (path == kUpload) Receive(client, request, address, fingerprint, parameters);
            else if (path == kCancel) CancelRequest(client, address, fingerprint, parameters);
            else client.Reply(Status::not_found, {}, options_.connect_timeout);
        } catch (const Cancelled&) {
        } catch (const std::exception&) {
            TryReply(client, Status::bad_request);
        } catch (const Error& error) {
            TryReply(client, error.code == TransferFailure::InvalidProtocol ? Status::bad_request
                                                                           : Status::internal_server_error);
        }
    }

    void TryReply(Https& client, Status status) {
        try {
            client.Reply(status, {}, options_.connect_timeout);
        } catch (const Error&) {
        } catch (const Cancelled&) {
        } catch (const std::exception&) {
        }
    }

    std::shared_ptr<Session> BuildSession(const Json& json, const net::ip::address& address,
                                          const std::optional<std::string>& fingerprint) {
        const auto& info = json.at("info");
        const auto& files = json.at("files");
        // Older HTTPS senders may omit their certificate or claim a random token.
        // Such a claim has no effect on the peer identity or trust state.
        auto peer = ParsePeer(info, address);
        if (!CompatibleVersion(info) || !files.is_object() || files.empty() ||
            files.size() > kMaxFiles || (!peer && fingerprint)) return {};
        auto session = std::make_shared<Session>();
        session->id = RandomToken();
        session->address = address;
        session->fingerprint = fingerprint;
        session->items.reserve(files.size());
        auto anonymous = info.value("alias", std::string("LocalSend sender"));
        anonymous.erase(std::remove_if(anonymous.begin(), anonymous.end(), [](unsigned char byte) {
            return byte < 32 || byte == 127;
        }), anonymous.end());
        if (anonymous.size() > 255) anonymous = "LocalSend sender";

        for (const auto& entry : files.items()) {
            const auto& dto = entry.value();
            const auto id = dto.at("id").get<std::string>();
            const auto name = dto.at("fileName").get<std::string>();
            const auto length = dto.at("size");
            const auto type = dto.at("fileType").get<std::string>();
            if (id != entry.key() || id.empty() || id.size() > 255 || !detail::ValidFileName(name) ||
                !length.is_number_integer() || length < 0) return {};
            Item item;
            item.wire_id = id;
            item.token = RandomToken();
            auto size = length.get<std::uint64_t>();
            if (files.size() == 1 && (type.compare(0, 5, "text/") == 0 || type == "text") &&
                dto.contains("preview") && dto["preview"].is_string()) {
                auto preview = dto["preview"].get<std::string>();
                if (size <= kLocalSendInlineTextLimit && preview.size() <= kLocalSendInlineTextLimit &&
                    detail::ValidUtf8(preview)) {
                    size = preview.size();
                    item.preview = std::move(preview);
                }
            }
            if (dto.contains("sha256") && !dto["sha256"].is_null()) {
                item.checksum = NormalizeFingerprint(dto["sha256"].get<std::string>());
                if (!item.checksum) return {};
            }
            const auto peer_id = fingerprint ? *fingerprint : "anonymous-" + session->id;
            item.offer = {{{kLocalSendProviderId, peer_id}, RandomToken()},
                {item.preview ? kTextTypeId : kFileTypeId, {}, name, size}};
            session->items.push_back(std::move(item));
        }
        if (fingerprint) {
            Remember(*peer, false);
        } else {
            sink_(Peer{session->items.front().offer.identity.peer, anonymous + " (unverified)",
                       false, {}, 2});
        }
        return session;
    }

    void Prepare(Https& client, const Json& json, const net::ip::address& address,
                 const std::optional<std::string>& fingerprint) {
        const auto& info = json.at("info");
        if (fingerprint && NormalizeFingerprint(info.value("fingerprint", "")) != fingerprint) {
            client.Reply(Status::forbidden, {}, options_.connect_timeout);
            sink_(IncomingConnectionFailure{kLocalSendProviderId, TransferFailure::PeerAuthenticationFailed,
                "LocalSend sender fingerprint does not match its client certificate"});
            return;
        }
        auto session = BuildSession(json, address, fingerprint);
        if (!session) {
            client.Reply(Status::bad_request, {}, options_.connect_timeout);
            return;
        }
        if (fingerprint) ReportIncomingPeer(address, info);
        bool busy;
        {
            std::lock_guard lock(mutex_);
            busy = static_cast<bool>(session_);
            if (!busy) session_ = session;
        }
        if (busy) {
            client.Reply(Status::conflict, {}, options_.connect_timeout);
            return;
        }

        try {
            const auto deadline = Clock::now() + options_.decision_timeout;
            auto accepted = Json::object();
            for (auto& item : session->items) {
                {
                    std::lock_guard lock(mutex_);
                    if (session->cancelled || stopping_) throw Cancelled{};
                    item.state = ItemState::Waiting;
                }
                sink_(item.offer);
                ItemState state;
                for (;;) {
                    std::unique_lock lock(mutex_);
                    if (stopping_ || session->cancelled) throw Cancelled{};
                    state = item.state;
                    if (state != ItemState::Waiting) break;
                    if (Clock::now() >= deadline) throw Error{TransferFailure::Timeout, "LocalSend offer expired"};
                    changed_.wait_for(lock, kPoll);
                    lock.unlock();
                    if (client.Disconnected()) throw Cancelled{};
                }
                if (state == ItemState::Finished) {
                    FinishItem(session, item, {TransferState::Declined, "recipient declined the offer"});
                } else if (item.preview) {
                    if (session->cancelled || stopping_) throw Cancelled{};
                    if (item.checksum) {
                        std::array<unsigned char, 32> digest{};
                        unsigned int length = 0;
                        if (EVP_Digest(item.preview->data(), item.preview->size(), digest.data(), &length,
                                       EVP_sha256(), nullptr) != 1 || length != digest.size() ||
                            Hex(digest.data(), length) != *item.checksum) {
                            FinishItem(session, item, Failed(TransferFailure::ChecksumMismatch,
                                                            "LocalSend text checksum mismatch"));
                            client.Reply(Status::unprocessable_entity, {}, options_.connect_timeout);
                            AbortSession(session, {TransferState::Cancelled});
                            return;
                        }
                    }
                    sink_(ReceivedContent{item.offer.identity, item.offer.metadata, TextPayload{*item.preview}});
                    FinishItem(session, item, {TransferState::Completed});
                    client.Reply(Status::no_content, {}, options_.connect_timeout);
                    AbortSession(session, {TransferState::Cancelled});
                    return;
                } else {
                    accepted[item.wire_id] = item.token;
                }
            }
            if (accepted.empty()) {
                client.Reply(Status::forbidden, {}, options_.connect_timeout);
                AbortSession(session, {TransferState::Cancelled});
                return;
            }
            {
                std::lock_guard lock(mutex_);
                session->preparing = false;
                session->expires = Clock::now() + options_.inactivity_timeout;
            }
            client.Reply(Status::ok, Json{{"sessionId", session->id}, {"files", accepted}}.dump(),
                         options_.connect_timeout);
        } catch (const Cancelled&) {
            AbortSession(session, {TransferState::Cancelled});
            if (!stopping_) TryReply(client, Status::forbidden);
        } catch (const Error& error) {
            AbortSession(session, Failed(error.code, error.reason));
            TryReply(client, error.code == TransferFailure::Timeout ? Status::forbidden
                                                                     : Status::internal_server_error);
        } catch (const std::exception& error) {
            AbortSession(session, Failed(TransferFailure::ConnectionFailure, error.what()));
            TryReply(client, Status::internal_server_error);
        }
    }

    void Receive(Https& client, Headers& request, const net::ip::address& address,
                 const std::optional<std::string>& fingerprint,
                 const std::map<std::string, std::string>& query) {
        for (const auto* field : {"sessionId", "fileId", "token"}) {
            if (!query.count(field)) {
                client.Reply(Status::bad_request, {}, options_.connect_timeout);
                return;
            }
        }
        std::shared_ptr<Session> session;
        Item* item = nullptr;
        FileDestinationFactory factory;
        Status failure = Status::forbidden;
        {
            std::lock_guard lock(mutex_);
            session = session_;
            if (session && session->id == query.at("sessionId") && session->address == address &&
                session->fingerprint == fingerprint && !session->cancelled) {
                if (session->preparing) failure = Status::conflict;
                else if (session->expires > Clock::now()) {
                    for (auto& candidate : session->items) {
                        if (candidate.wire_id == query.at("fileId") && candidate.token == query.at("token") &&
                            candidate.state == ItemState::Accepted) {
                            item = &candidate;
                            item->state = ItemState::Uploading;
                            factory = std::move(item->destination);
                            break;
                        }
                    }
                }
            }
        }
        if (!item) {
            client.Reply(failure, {}, options_.connect_timeout);
            return;
        }
        auto outcome = ReceiveFile(client, request, session, *item, factory);
        FinishItem(session, *item, std::move(outcome));
    }

    Outcome ReceiveFile(Https& client, Headers& request, const std::shared_ptr<Session>& session,
                        const Item& item, const FileDestinationFactory& factory) {
        const auto expected = *item.offer.metadata.byte_length;
        std::uint64_t total = 0;
        bool checksum_mismatch = false;
        client.SetCancellation(&session->cancelled);
        try {
            if (session->cancelled || stopping_) throw Cancelled{};
            if (request.content_length() && *request.content_length() != expected) {
                TryReply(client, Status::bad_request);
                return Failed(TransferFailure::InvalidProtocol, "unexpected LocalSend Content-Length");
            }
            auto destination = factory();
            if (!destination) throw Error{TransferFailure::FileFailure, "could not stage LocalSend file in Downloads"};
            auto& stream = destination->Stream();
            std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
            if (item.checksum && (!digest || EVP_DigestInit_ex(digest.get(), EVP_sha256(), nullptr) != 1)) {
                throw Error{TransferFailure::FileFailure, "could not initialize LocalSend checksum"};
            }
            if (request.get()[http::field::expect] == "100-continue") {
                client.Reply(Status::continue_, {}, options_.connect_timeout);
            }
            auto next = Clock::now();
            client.ReadBody(request, options_.inactivity_timeout, [&](const char* bytes, std::size_t size) {
                if (session->cancelled || stopping_) throw Cancelled{};
                if (size > expected - total) {
                    throw Error{TransferFailure::InvalidProtocol, "LocalSend file exceeded its declared size"};
                }
                if (!stream.write(bytes, static_cast<std::streamsize>(size)) ||
                    (item.checksum && EVP_DigestUpdate(digest.get(), bytes, size) != 1)) {
                    throw Error{TransferFailure::FileFailure, "could not write LocalSend staging file"};
                }
                total += size;
                {
                    std::lock_guard lock(mutex_);
                    session->expires = Clock::now() + options_.inactivity_timeout;
                }
                if (Clock::now() >= next || total == expected) {
                    Report(item.offer.identity, TransferDirection::Incoming, expected, {TransferState::InProgress}, total);
                    next = Clock::now() + std::chrono::milliseconds(100);
                }
            });
            if (session->cancelled || stopping_) throw Cancelled{};
            if (total != expected) throw Error{TransferFailure::InvalidProtocol, "LocalSend file was incomplete"};
            if (item.checksum) {
                std::array<unsigned char, 32> value{};
                unsigned int size = 0;
                if (EVP_DigestFinal_ex(digest.get(), value.data(), &size) != 1 || size != value.size()) {
                    throw Error{TransferFailure::FileFailure, "could not finalize LocalSend checksum"};
                }
                checksum_mismatch = Hex(value.data(), size) != *item.checksum;
                if (checksum_mismatch) throw Error{TransferFailure::ChecksumMismatch, "LocalSend checksum mismatch"};
            }
            if (session->cancelled || stopping_) throw Cancelled{};
            auto result = destination->Commit();
            if (!result.accepted) throw Error{TransferFailure::FileFailure, result.reason};
            destination.reset();
            client.SetCancellation(nullptr);
            TryReply(client, Status::ok);
            return {TransferState::Completed, std::move(result.reason)};
        } catch (const Cancelled&) {
            client.SetCancellation(nullptr);
            TryReply(client, Status::forbidden);
            return {TransferState::Cancelled};
        } catch (const Error& error) {
            client.SetCancellation(nullptr);
            TryReply(client, checksum_mismatch ? Status::unprocessable_entity :
                error.code == TransferFailure::InvalidProtocol ? Status::bad_request : Status::internal_server_error);
            return Failed(error.code, error.reason);
        } catch (const std::exception& error) {
            client.SetCancellation(nullptr);
            TryReply(client, Status::internal_server_error);
            return Failed(TransferFailure::FileFailure, error.what());
        }
    }

    void CancelRequest(Https& client, const net::ip::address& address,
                       const std::optional<std::string>& fingerprint,
                       const std::map<std::string, std::string>& query) {
        std::shared_ptr<Session> session;
        {
            std::lock_guard lock(mutex_);
            session = session_;
            if (session && (session->address != address || session->fingerprint != fingerprint ||
                ((!session->preparing || query.count("sessionId")) &&
                 (!query.count("sessionId") || query.at("sessionId") != session->id)))) session.reset();
        }
        if (session) AbortSession(session, {TransferState::Cancelled});
        client.Reply(session ? Status::ok : Status::forbidden, {}, options_.connect_timeout);
    }

    std::string WaitForPin(Outgoing& outgoing) {
        {
            std::lock_guard lock(mutex_);
            outgoing.awaiting_pin = true;
        }
        Report(outgoing.transfer.identity, TransferDirection::Outgoing, *outgoing.transfer.metadata.byte_length,
               {TransferState::Pending, "Enter the LocalSend receiver PIN and retry", TransferFailure::PinRequired});
        std::unique_lock lock(mutex_);
        changed_.wait_for(lock, options_.decision_timeout, [&] {
            return stopping_ || outgoing.cancelled || outgoing.pin;
        });
        outgoing.awaiting_pin = false;
        if (stopping_ || outgoing.cancelled) throw Cancelled{};
        if (!outgoing.pin) throw Error{TransferFailure::Timeout, "LocalSend PIN entry timed out"};
        return std::exchange(outgoing.pin, {}).value();
    }

    Outcome Transmit(Outgoing& outgoing, std::string& session_id) {
        const auto& transfer = outgoing.transfer;
        const auto length = *transfer.metadata.byte_length;
        const auto* text = std::get_if<TextPayload>(&transfer.payload);
        const auto id = RandomToken();
        const auto name = text ? id + ".txt" : *transfer.metadata.display_name;
        const bool text_file = name.size() >= 4 && name.compare(name.size() - 4, 4, ".txt") == 0;
        auto file = Json{{"id", id}, {"fileName", name}, {"size", length},
                         {"fileType", text_file ? "text/plain" : "application/octet-stream"}};
        if (text) file["preview"] = text->utf8;
        const auto body = Json{{"info", Info()}, {"files", Json{{id, file}}}}.dump();
        auto target = std::string(kPrepare);
        http::response<http::string_body> response;
        for (;;) {
            {
                Https http(*context_, stopping_, &outgoing.cancelled);
                http.Connect(outgoing.address, transfer.identity.peer.peer_id, options_.connect_timeout);
                http.Post(target, body, options_.connect_timeout);
                response = http.ReadResponse(options_.decision_timeout, "No reply to the LocalSend offer before it expired");
            }
            ReportVerifiedPeer(outgoing.address.address());
            if (response.result() != Status::unauthorized) break;
            target = std::string(kPrepare) + "?pin=" + Encode(WaitForPin(outgoing));
        }
        if (text && response.result() == Status::no_content) return {TransferState::Completed};
        if (response.result() != Status::ok) return ResponseFailure(response.result(), true);
        const auto accepted = Parse(response.body());
        session_id = accepted.at("sessionId").get<std::string>();
        if (session_id.empty() || session_id.size() > 255) {
            session_id.clear();
            return Failed(TransferFailure::InvalidProtocol, "invalid LocalSend session identifier");
        }
        const auto& files = accepted.at("files");
        if (!files.is_object() || !files.contains(id)) return {TransferState::Declined, "LocalSend item was not accepted"};
        const auto token = files.at(id).get<std::string>();
        if (token.empty() || token.size() > 255) return Failed(TransferFailure::InvalidProtocol, "invalid LocalSend upload token");
        // Older receivers can ask to upload the body even for an inline offer.
        std::istringstream inline_source(text ? text->utf8 : "");
        auto& source = text ? static_cast<std::istream&>(inline_source)
                            : *std::get<std::unique_ptr<std::istream>>(transfer.payload);
        Https http(*context_, stopping_, &outgoing.cancelled);
        http.Connect(outgoing.address, transfer.identity.peer.peer_id, options_.connect_timeout);
        auto next = Clock::now();
        http.Upload(std::string(kUpload) + "?sessionId=" + Encode(session_id) + "&fileId=" + Encode(id) +
            "&token=" + Encode(token), source, length, options_.inactivity_timeout, [&](std::uint64_t total) {
                if (Clock::now() >= next || total == length) {
                    Report(transfer.identity, TransferDirection::Outgoing, length, {TransferState::InProgress}, total);
                    next = Clock::now() + std::chrono::milliseconds(100);
                }
            });
        response = http.ReadResponse(options_.inactivity_timeout);
        return response.result() == Status::ok || response.result() == Status::no_content
            ? Outcome{TransferState::Completed} : ResponseFailure(response.result(), false);
    }

    void SendLoop() {
        for (;;) {
            std::shared_ptr<Outgoing> outgoing;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [&] { return stopping_ || outgoing_; });
                outgoing = outgoing_;
                if (!outgoing) return;
            }
            std::string session_id;
            Outcome result{TransferState::Cancelled};
            try {
                result = Transmit(*outgoing, session_id);
            } catch (const Cancelled&) {
            } catch (const Error& error) {
                result = Failed(error.code, error.reason);
            } catch (const std::exception& error) {
                result = Failed(TransferFailure::InvalidProtocol, error.what());
            }
            if (!session_id.empty() && result.state != TransferState::Completed && !stopping_) {
                try {
                    Https http(*context_, stopping_);
                    http.Connect(outgoing->address, outgoing->transfer.identity.peer.peer_id, options_.connect_timeout);
                    http.Post(std::string(kCancel) + "?sessionId=" + Encode(session_id), {}, options_.connect_timeout);
                    http.ReadResponse(options_.connect_timeout);
                } catch (const Cancelled&) {
                } catch (const Error&) {
                } catch (const std::exception&) {
                }
            }
            {
                std::lock_guard lock(mutex_);
                outgoing_.reset();
            }
            Report(outgoing->transfer.identity, TransferDirection::Outgoing,
                   *outgoing->transfer.metadata.byte_length, std::move(result));
            if (stopping_) return;
        }
    }

    const LocalSendOptions options_;
    std::unique_ptr<ssl::context> context_;
    net::io_context sockets_;
    tcp::acceptor acceptor_{sockets_};
    Udp::socket discovery_{sockets_};
    Udp::endpoint group_;
    std::string fingerprint_;
    std::uint16_t receive_port_ = 0;
    std::atomic<bool> stopping_{true};
    std::atomic<bool> refresh_requested_{false};
    bool unicast_refresh_requested_ = false;
    std::thread browser_, unicast_browser_, server_, sender_;
    ProviderEventSink sink_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::optional<tcp::endpoint> manual_endpoint_;
    std::map<std::string, PeerRecord> peers_;
    LocalSendDiscoveryStatus discovery_status_;
    std::shared_ptr<Session> session_;
    std::shared_ptr<Outgoing> outgoing_;
};

} // namespace
} // namespace localsend

std::unique_ptr<ILocalSendProvider> MakeLocalSendProvider(LocalSendOptions options) {
    return std::make_unique<localsend::LocalSendProvider>(std::move(options));
}

} // namespace textlt::connect
