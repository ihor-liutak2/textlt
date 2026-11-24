#pragma once

#include "protocol.hpp"

#include <map>
#include <mutex>

namespace textlt::connect::detail {

struct PeerEndpoint {
    std::string ip;
    std::uint16_t port;
};

bool ValidDiscoveryId(std::string_view id);
bool ValidDiscoveryName(std::string_view name);

class Discovery {
public:
    Discovery(const TextLTDiscoveryOptions& options, std::uint16_t listen_port);
    void Run(const std::atomic<bool>& stopping, const ProviderEventSink& sink);
    std::optional<PeerEndpoint> FindCompatiblePeer(const std::string& id) const;

private:
    struct Entry {
        Peer peer;
        PeerEndpoint endpoint;
        Deadline expires;
        Deadline files_expire;
    };

    void Receive(const ProviderEventSink& sink);
    void Expire(const ProviderEventSink& sink);

    Socket socket_;
    std::string local_id_;
    std::string announcement_;
    std::string file_announcement_;
    mutable std::mutex mutex_;
    std::map<std::string, Entry> peers_;
};

} // namespace textlt::connect::detail
