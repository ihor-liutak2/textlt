#pragma once

#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace textlt::connect {

inline constexpr char kTextTypeId[] = "connect.text";
inline constexpr char kFileTypeId[] = "connect.file";

using SchemaVersion = std::uint32_t;

struct PeerIdentity {
    std::string provider_id;
    std::string peer_id;
};

struct Capability {
    std::string type_id;
    // Empty for unversioned capabilities. Structured types advertise positive versions.
    std::vector<SchemaVersion> schema_versions;
};

struct Peer {
    PeerIdentity identity;
    std::string display_name;
    bool online = false;
    std::vector<Capability> capabilities;
    std::optional<std::uint32_t> protocol_version = std::nullopt;
};

struct TransferIdentity {
    PeerIdentity peer;
    // Unique among transfers in the same direction with this peer.
    std::string transfer_id;
};

struct TransferMetadata {
    std::string type_id;
    // Required for structured payloads, absent for ordinary text and files.
    std::optional<SchemaVersion> schema_version;
    std::optional<std::string> display_name;
    std::optional<std::uint64_t> byte_length;
};

struct TextPayload {
    std::string utf8;
};

struct StructuredPayload {
    std::vector<std::uint8_t> bytes;
};

struct IncomingOffer {
    TransferIdentity identity;
    TransferMetadata metadata;
};

using OutgoingPayload = std::variant<TextPayload, std::unique_ptr<std::istream>,
                                     StructuredPayload>;

struct OutgoingTransfer {
    TransferIdentity identity;
    TransferMetadata metadata;
    // A file stream is opened in binary mode and positioned at the start.
    OutgoingPayload payload;
};

enum class TransferDirection { Incoming, Outgoing };
enum class TransferState { Pending, InProgress, Completed, Declined, Cancelled, Failed };

enum class TransferFailure {
    Timeout,
    ConnectionFailure,
    UnsupportedProtocolVersion,
    InvalidProtocol,
    PeerAuthenticationFailed,
    TlsFailure,
    FileFailure,
    UnsupportedContentType,
    UnsupportedSchemaVersion,
    InvalidStructuredPayload,
    PinRequired,
    Busy,
    RateLimited,
    ChecksumMismatch
};

struct TransferEvent {
    TransferIdentity identity;
    TransferDirection direction;
    TransferState state;
    std::optional<std::uint64_t> bytes_transferred;
    std::optional<std::uint64_t> total_bytes;
    std::optional<std::string> reason;
    std::optional<TransferFailure> failure = std::nullopt;
};

struct ReceivedContent {
    TransferIdentity identity;
    TransferMetadata metadata;
    std::variant<TextPayload, StructuredPayload> payload;
};

// An inbound connection failed before it had a transfer identity.
struct IncomingConnectionFailure {
    std::string provider_id;
    TransferFailure failure;
    std::string reason;
};

struct PeerCapabilities {
    PeerIdentity identity;
    std::vector<Capability> capabilities;
};

struct LocalSendConnectResult {
    std::optional<PeerIdentity> peer;
    std::string message;
};

using ProviderEvent = std::variant<Peer, IncomingOffer, TransferEvent, ReceivedContent,
                                   IncomingConnectionFailure, PeerCapabilities, LocalSendConnectResult>;

} // namespace textlt::connect
