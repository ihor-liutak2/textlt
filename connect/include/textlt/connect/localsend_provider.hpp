#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "textlt/connect/provider.hpp"

namespace textlt::connect {

inline constexpr char kLocalSendProviderId[] = "localsend";
inline constexpr std::size_t kLocalSendInlineTextLimit = 16384;

enum class LocalSendCheck { NotAttempted, Succeeded, Failed };

struct LocalSendRuntime {
    LocalSendCheck udp_bind = LocalSendCheck::NotAttempted;
    LocalSendCheck https_bind = LocalSendCheck::NotAttempted;
    LocalSendCheck multicast_join = LocalSendCheck::NotAttempted;
    std::optional<std::uint16_t> udp_port;
    std::optional<std::uint16_t> https_port;
    bool listening = false;
    std::string interface_ip;
    std::string error;
};

struct LocalSendSubnet {
    std::string address;
    int prefix_length;
};

struct LocalSendDiscoveryStatus {
    LocalSendCheck multicast_send = LocalSendCheck::NotAttempted;
    std::string multicast_error;
    unsigned multicast_packets = 0;
    std::string unicast_subnet;
    bool unicast_running = false;
    unsigned unicast_attempts = 0;
    unsigned unicast_registered = 0;
};

struct LocalSendOptions {
    std::filesystem::path identity_file;
    std::string alias = "TextLT";
    // 0.0.0.0 selects the system's default multicast interface.
    std::string interface_ip = "0.0.0.0";
    std::string listen_ip = "0.0.0.0";
    std::string multicast_address = "224.0.0.167";
    std::uint16_t discovery_port = 53317;
    std::uint16_t receive_port = 53317; // Zero asks the OS for a free receive port.
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds decision_timeout{120000};
    std::chrono::milliseconds inactivity_timeout{30000};
    // Start and Stop report a snapshot on their caller's thread.
    std::function<void(LocalSendRuntime)> report_runtime;
    // Exclude same-machine addresses when collecting reachability evidence.
    std::vector<std::string> local_addresses;
    // Runs on a worker thread after verified HTTPS traffic with a non-local address.
    std::function<void()> report_verified_peer;
    // Runs on a worker thread after an authenticated mobile peer reaches the listener.
    std::function<void()> report_incoming_mobile_peer;
    // Runs on the discovery worker. Supply a LAN subnet when multicast cannot reach it.
    std::function<std::optional<LocalSendSubnet>()> unicast_discovery_subnet;
    // Runs on a worker thread when discovery diagnostics change.
    std::function<void()> report_discovery_change;
};

class ILocalSendProvider : public IConnectProvider {
public:
    // Resolves an IPv4 address with an optional port. Completion arrives as a LocalSendConnectResult.
    virtual ProviderResult ConnectByIp(std::string_view endpoint) = 0;
    virtual ProviderResult RefreshPeers() = 0;
    virtual LocalSendDiscoveryStatus DiscoveryStatus() const = 0;
    virtual std::vector<Peer> OnlinePeers() const = 0;
};

std::unique_ptr<ILocalSendProvider> MakeLocalSendProvider(LocalSendOptions options);

} // namespace textlt::connect
