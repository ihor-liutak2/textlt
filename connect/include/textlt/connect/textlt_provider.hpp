#pragma once

#include <chrono>

#include "textlt/connect/provider.hpp"

namespace textlt::connect {

inline constexpr char kTextLTProviderId[] = "textlt";
inline constexpr char kTextLTManualPeerId[] = "manual-peer";

struct TextLTTlsOptions {
    // File paths to the local certificate and its unencrypted private key.
    std::string certificate_pem;
    std::string private_key_pem;
    // SHA-256 fingerprint of the peer's leaf certificate. Hex with or without colons.
    std::string expected_peer_fingerprint;
};

struct TextLTDiscoveryOptions {
    std::string interface_ip;
    // Required for advertising a listener. Use a unique, persistent ID per device.
    std::string local_peer_id;
    std::string display_name;
};

struct TextLTProviderOptions {
    // A zero port disables the corresponding direction.
    std::uint16_t listen_port = 0;
    std::uint16_t peer_port = 0;
    std::chrono::milliseconds timeout{5000};
    std::string listen_ip = "127.0.0.1";
    std::string peer_ip = "127.0.0.1";
    // Required for any endpoint outside the development loopback address.
    std::optional<TextLTTlsOptions> tls = std::nullopt;
    std::optional<TextLTDiscoveryOptions> discovery = std::nullopt;
    // Associates the configured TLS pin with one discovery ID. Announcements are untrusted.
    std::optional<std::string> trusted_discovery_peer_id = std::nullopt;
    bool probe_manual_peer = false;
    // Immutable for a running provider. Validation runs on the transfer worker.
    std::vector<Capability> structured_capabilities{};
    std::function<bool(const TransferMetadata&, const StructuredPayload&)> validate_structured{};
};

// Reads a PEM leaf certificate and returns its SHA-256 fingerprint as colon-separated hex.
std::optional<std::string> TextLTCertificateFingerprint(const std::string& certificate_pem);

// Call Start and Stop from the owner thread. Event sinks can run concurrently on worker threads and
// must return promptly without throwing. A sink may call Accept, Decline, Send, or Cancel.
std::unique_ptr<IConnectProvider> MakeTextLTProvider(TextLTProviderOptions options);

} // namespace textlt::connect
