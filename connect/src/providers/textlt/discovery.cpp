#include "discovery.hpp"

#include <algorithm>
#include <thread>
#include <utility>

namespace textlt::connect::detail {
namespace {

constexpr std::string_view kMagic = "TLTD";
constexpr std::string_view kFileMagic = "TLTF";
constexpr char kDiscoveryVersion = 1;
constexpr std::size_t kHeaderSize = 10;
constexpr std::size_t kMaxName = 96;
constexpr std::size_t kMaxPeers = 128;
constexpr auto kAnnounceInterval = std::chrono::seconds(1);
constexpr auto kPeerLifetime = std::chrono::seconds(5);

unsigned char Byte(std::string_view bytes, std::size_t index) {
    return static_cast<unsigned char>(bytes[index]);
}

} // namespace

bool ValidDiscoveryId(std::string_view id) {
    return id != kTextLTManualPeerId && ValidTransferId(id);
}

bool ValidDiscoveryName(std::string_view name) {
    return !name.empty() && name.size() <= kMaxName && ValidUtf8(name) &&
           std::none_of(name.begin(), name.end(), [](unsigned char byte) {
               return byte < 32 || byte == 127;
           });
}

Discovery::Discovery(const TextLTDiscoveryOptions& options, std::uint16_t listen_port)
    : socket_(Socket::JoinMulticast(options.interface_ip)), local_id_(options.local_peer_id) {
    if (listen_port != 0) {
        announcement_ = kMagic;
        announcement_.push_back(kDiscoveryVersion);
        announcement_.push_back(static_cast<char>(kTextProtocolVersion));
        announcement_.push_back(static_cast<char>(listen_port >> 8));
        announcement_.push_back(static_cast<char>(listen_port));
        announcement_.push_back(static_cast<char>(local_id_.size()));
        announcement_.push_back(static_cast<char>(options.display_name.size()));
        announcement_ += local_id_;
        announcement_ += options.display_name;
        file_announcement_ = announcement_;
        file_announcement_.replace(0, kMagic.size(), kFileMagic);
        file_announcement_[5] = static_cast<char>(kFileProtocolVersion);
    }
}

std::optional<PeerEndpoint> Discovery::FindCompatiblePeer(const std::string& id) const {
    std::lock_guard lock(mutex_);
    const auto entry = peers_.find(id);
    if (entry == peers_.end() || entry->second.expires <= Clock::now() ||
        (entry->second.peer.protocol_version != kTextProtocolVersion &&
         entry->second.peer.protocol_version != kFileProtocolVersion)) {
        return std::nullopt;
    }
    return entry->second.endpoint;
}

void Discovery::Receive(const ProviderEventSink& sink) {
    char buffer[kHeaderSize + kMaxTransferId + kMaxName + 1];
    std::string ip;
    const int count = socket_.ReceiveAnnouncement(buffer, sizeof(buffer), ip);
    const std::string_view bytes(buffer, static_cast<std::size_t>(count));
    const auto magic = bytes.substr(0, kMagic.size());
    const bool file_announcement = magic == kFileMagic;
    if (bytes.size() < kHeaderSize || (!file_announcement && magic != kMagic) ||
        bytes[4] != kDiscoveryVersion || !IsLanIPv4(ip) ||
        (file_announcement && Byte(bytes, 5) != kFileProtocolVersion)) {
        return;
    }
    const auto port = static_cast<std::uint16_t>((Byte(bytes, 6) << 8) | Byte(bytes, 7));
    const auto id_length = Byte(bytes, 8);
    const auto name_length = Byte(bytes, 9);
    if (port == 0 || Byte(bytes, 5) == 0 || bytes.size() != kHeaderSize + id_length + name_length) {
        return;
    }
    const auto id = bytes.substr(kHeaderSize, id_length);
    const auto name = bytes.substr(kHeaderSize + id_length, name_length);
    if (id == local_id_ || !ValidDiscoveryId(id) || !ValidDiscoveryName(name)) {
        return;
    }

    Peer peer{{kTextLTProviderId, std::string(id)}, std::string(name), true,
              {{kTextTypeId, {}}}, Byte(bytes, 5)};
    {
        std::lock_guard lock(mutex_);
        const auto& key = peer.identity.peer_id;
        const auto previous = peers_.find(key);
        if (previous == peers_.end() && peers_.size() == kMaxPeers) {
            return;
        }
        const auto now = Clock::now();
        const bool same_endpoint = previous != peers_.end() &&
            previous->second.endpoint.ip == ip && previous->second.endpoint.port == port;
        const auto files_expire = file_announcement ? now + kPeerLifetime :
            same_endpoint ? previous->second.files_expire : Deadline{};
        if (files_expire > now) {
            peer.protocol_version = kFileProtocolVersion;
            peer.capabilities.push_back({kFileTypeId, {}});
        }
        const bool changed = !same_endpoint || previous->second.peer.display_name != name ||
            previous->second.peer.protocol_version != peer.protocol_version;
        peers_.insert_or_assign(key, Entry{peer, {std::move(ip), port}, now + kPeerLifetime, files_expire});
        if (!changed) {
            return;
        }
    }
    sink(peer);
}

void Discovery::Expire(const ProviderEventSink& sink) {
    std::vector<Peer> expired;
    {
        std::lock_guard lock(mutex_);
        const auto now = Clock::now();
        for (auto peer = peers_.begin(); peer != peers_.end();) {
            if (peer->second.expires > now) {
                ++peer;
                continue;
            }
            peer->second.peer.online = false;
            expired.push_back(std::move(peer->second.peer));
            peer = peers_.erase(peer);
        }
    }
    for (auto& peer : expired) {
        sink(std::move(peer));
    }
}

void Discovery::Run(const std::atomic<bool>& stopping, const ProviderEventSink& sink) {
    auto next_announcement = Clock::now();
    while (!stopping) {
        const auto now = Clock::now();
        if (!announcement_.empty() && now >= next_announcement) {
            socket_.Announce(file_announcement_);
            socket_.Announce(announcement_);
            next_announcement = now + kAnnounceInterval;
        }
        Expire(sink);
        try {
            if (socket_.Ready(false, kPollMilliseconds) && !stopping) {
                Receive(sink);
            }
        } catch (IoError) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMilliseconds));
        }
    }
}

} // namespace textlt::connect::detail
