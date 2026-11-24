#include "textlt/connect/textlt_provider.hpp"

#include <charconv>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string_view>

namespace {

using namespace textlt::connect;

const char* FailureName(std::optional<TransferFailure> failure) {
    if (!failure) {
        return "failed";
    }
    switch (*failure) {
    case TransferFailure::Timeout: return "timeout";
    case TransferFailure::ConnectionFailure: return "connection failure";
    case TransferFailure::UnsupportedProtocolVersion: return "unsupported protocol version";
    case TransferFailure::InvalidProtocol: return "invalid protocol";
    case TransferFailure::PeerAuthenticationFailed: return "peer authentication failed";
    case TransferFailure::FileFailure: return "file failure";
    case TransferFailure::UnsupportedContentType: return "unsupported content type";
    case TransferFailure::UnsupportedSchemaVersion: return "unsupported schema version";
    case TransferFailure::InvalidStructuredPayload: return "invalid structured payload";
    case TransferFailure::PinRequired: return "PIN required";
    case TransferFailure::Busy: return "busy";
    case TransferFailure::RateLimited: return "rate limited";
    case TransferFailure::ChecksumMismatch: return "checksum mismatch";
    case TransferFailure::TlsFailure: return "TLS failure";
    }
    return "failed";
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    const bool listing = mode == "discover";
    const bool send_discovered = mode == "send-discovered";
    const bool advertise = mode == "receive-discovery";
    const bool sending = mode == "send" || mode == "send-tls" || send_discovered;
    const bool receiving = mode == "receive" || mode == "receive-tls" || advertise;
    const bool tls = mode == "send-tls" || mode == "receive-tls" || send_discovered || advertise;
    const int expected = listing ? 3 : advertise ? 11 : tls ? 8 : 4;
    if ((!sending && !receiving && !listing) || argc != expected ||
        (receiving && std::string_view(argv[argc - 1]) != "accept" &&
         std::string_view(argv[argc - 1]) != "decline")) {
        std::cerr << "Usage: textlt_connect_loopback send PORT MESSAGE\n"
                     "       textlt_connect_loopback receive PORT accept|decline\n"
                     "       textlt_connect_loopback send-tls IP PORT CERT KEY PEER_PIN MESSAGE\n"
                     "       textlt_connect_loopback receive-tls IP PORT CERT KEY PEER_PIN accept|decline\n"
                     "       textlt_connect_loopback discover INTERFACE_IP\n"
                     "       textlt_connect_loopback send-discovered INTERFACE_IP PEER_ID CERT KEY PEER_PIN MESSAGE\n"
                     "       textlt_connect_loopback receive-discovery IP PORT LOCAL_ID NAME CERT KEY PEER_ID PEER_PIN accept|decline\n";
        return 1;
    }

    const std::string ip = tls || listing ? argv[2] : "127.0.0.1";
    std::uint16_t port = 0;
    if (!listing && !send_discovered) {
        const std::string_view port_text = argv[tls ? 3 : 2];
        const auto end = port_text.data() + port_text.size();
        const auto parsed = std::from_chars(port_text.data(), end, port);
        if (parsed.ec != std::errc{} || parsed.ptr != end || port == 0) {
            std::cerr << "Port must be between 1 and 65535.\n";
            return 1;
        }
    }

    std::mutex mutex;
    std::condition_variable changed;
    std::optional<int> exit_code;
    bool peer_found = false;
    auto finish = [&](int code) {
        {
            std::lock_guard lock(mutex);
            exit_code = code;
        }
        changed.notify_one();
    };

    TextLTProviderOptions options;
    if (receiving) {
        options.listen_port = port;
        options.listen_ip = ip;
    } else if (sending && !send_discovered) {
        options.peer_port = port;
        options.peer_ip = ip;
    }
    if (advertise) {
        options.discovery = TextLTDiscoveryOptions{ip, argv[4], argv[5]};
        options.tls = TextLTTlsOptions{argv[6], argv[7], argv[9]};
        options.trusted_discovery_peer_id = argv[8];
    } else if (tls) {
        options.tls = TextLTTlsOptions{argv[4], argv[5], argv[6]};
    }
    if (listing || send_discovered) {
        options.discovery = TextLTDiscoveryOptions{ip, {}, {}};
    }
    if (send_discovered) {
        options.trusted_discovery_peer_id = argv[3];
    }
    const auto target_id = options.trusted_discovery_peer_id.value_or(kTextLTManualPeerId);
    auto provider = MakeTextLTProvider(options);
    const bool accept = receiving && std::string_view(argv[argc - 1]) == "accept";
    const auto started = provider->Start([&](ProviderEvent event) {
        if (const auto* peer = std::get_if<Peer>(&event)) {
            if (listing || (send_discovered && peer->identity.peer_id == target_id)) {
                const auto version = peer->protocol_version.value_or(0);
                std::cout << (peer->online ? "Online " : "Offline ") << peer->identity.peer_id
                          << " (" << peer->display_name << "), protocol " << version << std::endl;
                if (send_discovered) {
                    {
                        std::lock_guard lock(mutex);
                        peer_found = peer->online;
                    }
                    changed.notify_one();
                }
            }
        } else if (const auto* offer = std::get_if<IncomingOffer>(&event)) {
            std::cout << "Offer " << offer->identity.transfer_id << " ("
                      << *offer->metadata.byte_length << " bytes)\n";
            const auto decision = accept ? provider->Accept(offer->identity, nullptr) :
                provider->Decline(offer->identity, "declined by receiver");
            if (!decision.accepted) {
                std::cerr << decision.reason << '\n';
                finish(1);
            }
        } else if (const auto* content = std::get_if<ReceivedContent>(&event)) {
            std::cout << "Received: " << std::get<TextPayload>(content->payload).utf8 << '\n';
        } else if (const auto* failure = std::get_if<IncomingConnectionFailure>(&event)) {
            std::cerr << FailureName(failure->failure) << ": " << failure->reason << '\n';
            finish(1);
        } else if (const auto* transfer = std::get_if<TransferEvent>(&event)) {
            switch (transfer->state) {
            case TransferState::Completed:
                std::cout << (sending ? "Delivered " : "Acknowledged ")
                          << transfer->identity.transfer_id << '\n';
                finish(0);
                break;
            case TransferState::Declined:
                std::cout << "Declined " << transfer->identity.transfer_id << '\n';
                finish(sending ? 2 : 0);
                break;
            case TransferState::Failed:
            case TransferState::Cancelled:
                std::cerr << (transfer->state == TransferState::Cancelled ? "cancelled" :
                              FailureName(transfer->failure)) << ": "
                          << transfer->reason.value_or("") << '\n';
                finish(1);
                break;
            case TransferState::Pending:
            case TransferState::InProgress:
                break;
            }
        }
    });
    if (!started.accepted) {
        std::cerr << started.reason << '\n';
        return 1;
    }

    if (listing) {
        std::unique_lock lock(mutex);
        changed.wait_for(lock, std::chrono::seconds(8), [&] { return exit_code.has_value(); });
        return 0;
    }
    if (send_discovered) {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(8), [&] { return peer_found; })) {
            std::cerr << "No announcement from " << target_id << '\n';
            return 1;
        }
    }

    if (sending) {
        const auto submitted = provider->Send({
            {{kTextLTProviderId, target_id}, "message-1"},
            {kTextTypeId, std::nullopt, std::nullopt, std::nullopt},
            TextPayload{argv[argc - 1]}
        });
        if (!submitted.accepted) {
            std::cerr << submitted.reason << '\n';
            return 1;
        }
    } else {
        std::cout << "Listening on " << ip << ':' << port << std::endl;
    }

    {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return exit_code.has_value(); });
    }
    provider->Stop();
    return *exit_code;
}
