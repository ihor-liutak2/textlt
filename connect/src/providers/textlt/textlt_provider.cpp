#include "textlt/connect/textlt_provider.hpp"

#include "discovery.hpp"
#include "../../browser/server.hpp"
#include "../localsend/errors.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>

namespace textlt::connect {
namespace {

using namespace detail;

constexpr TransferDirection kIncoming = TransferDirection::Incoming;
constexpr TransferDirection kOutgoing = TransferDirection::Outgoing;

bool SameTransfer(const TransferIdentity& left, const TransferIdentity& right) {
    return left.peer.provider_id == right.peer.provider_id &&
           left.peer.peer_id == right.peer.peer_id && left.transfer_id == right.transfer_id;
}

TransferMetadata TextMetadata(std::size_t length) {
    return {kTextTypeId, std::nullopt, std::nullopt, length};
}

struct FileError {
    std::string reason;
};

constexpr std::size_t kFileChunkSize = 64 * 1024;

struct Outcome {
    TransferState state;
    std::optional<std::string> reason = std::nullopt;
    std::optional<TransferFailure> failure = std::nullopt;
};

Outcome Failure(TransferFailure failure, const char* reason) {
    return {TransferState::Failed, reason, failure};
}

std::optional<Outcome> ContentFailure(Reply reply) {
    if (reply == Reply::UnknownType) {
        return Failure(TransferFailure::UnsupportedContentType, "peer does not support this content type");
    }
    if (reply == Reply::UnsupportedSchema) {
        return Failure(TransferFailure::UnsupportedSchemaVersion, "peer does not support this schema version");
    }
    if (reply == Reply::InvalidPayload) {
        return Failure(TransferFailure::InvalidStructuredPayload, "structured payload failed validation");
    }
    return std::nullopt;
}

Outcome FromIoError(IoError error) {
    switch (error) {
    case IoError::Cancelled:
        return {TransferState::Cancelled};
    case IoError::Timeout:
        return Failure(TransferFailure::Timeout, "transfer timed out");
    case IoError::Connection:
        return Failure(TransferFailure::ConnectionFailure, "connection failed or closed early");
    }
    return Failure(TransferFailure::ConnectionFailure, "connection failed");
}

Outcome FromProtocolError(ProtocolError error) {
    if (error == ProtocolError::UnsupportedVersion) {
        return Failure(TransferFailure::UnsupportedProtocolVersion, "unsupported protocol version");
    }
    return Failure(TransferFailure::InvalidProtocol, "invalid TextLT protocol data");
}

class TextLTProvider final : public IConnectProvider {
public:
    explicit TextLTProvider(TextLTProviderOptions options) : options_(std::move(options)) {}
    ~TextLTProvider() override { Stop(); }

    std::string Id() const override { return kTextLTProviderId; }
    std::vector<Capability> Capabilities() const override {
        std::vector<Capability> capabilities{{kTextTypeId, {}}};
        if (options_.tls || options_.browser) {
            capabilities.push_back({kFileTypeId, {}});
        }
        if (options_.tls) {
            capabilities.insert(capabilities.end(), options_.structured_capabilities.begin(),
                                options_.structured_capabilities.end());
        }
        return capabilities;
    }

    ProviderResult Start(ProviderEventSink sink) override {
        std::unique_lock lock(mutex_);
        if (worker_.joinable()) {
            return {false, "provider is already started"};
        }
        if (!sink || (!options_.listen_port && !options_.peer_port && !options_.discovery && !options_.browser) ||
            options_.timeout.count() <= 0 || options_.timeout > std::chrono::hours(1)) {
            return {false, "invalid provider options or event sink"};
        }

        if ((options_.listen_port && !IsUnicastIPv4(options_.listen_ip)) ||
            (options_.peer_port && !IsUnicastIPv4(options_.peer_ip))) {
            return {false, "endpoints require specific numeric unicast IPv4 addresses"};
        }
        if (!options_.tls && ((options_.listen_port && options_.listen_ip != "127.0.0.1") ||
                              (options_.peer_port && options_.peer_ip != "127.0.0.1"))) {
            return {false, "LAN endpoints require mutual TLS and an expected peer fingerprint"};
        }

        if (options_.trusted_discovery_peer_id &&
            (!options_.tls || !ValidDiscoveryId(*options_.trusted_discovery_peer_id))) {
            return {false, "a trusted discovery peer requires a valid peer ID and TLS fingerprint"};
        }
        if (const auto& discovery = options_.discovery) {
            if (!IsLanIPv4(discovery->interface_ip) ||
                (!discovery->local_peer_id.empty() && !ValidDiscoveryId(discovery->local_peer_id)) ||
                (options_.trusted_discovery_peer_id &&
                 *options_.trusted_discovery_peer_id == discovery->local_peer_id)) {
                return {false, "discovery requires a LAN interface and distinct valid peer IDs"};
            }
            if (options_.listen_port &&
                (!options_.tls || options_.listen_ip != discovery->interface_ip ||
                 discovery->local_peer_id.empty() || !ValidDiscoveryName(discovery->display_name))) {
                return {false, "advertising requires a TLS listener on the discovery interface, "
                               "a local peer ID, and a valid display name"};
            }
        }

        const auto& capabilities = options_.structured_capabilities;
        if ((!capabilities.empty() && !options_.validate_structured) ||
            capabilities.size() > kMaxCapabilities - 2 ||
            std::any_of(capabilities.begin(), capabilities.end(), [](const Capability& capability) {
                return !ValidTypeId(capability.type_id) || capability.type_id == kTextTypeId ||
                    capability.type_id == kFileTypeId || capability.schema_versions.empty() ||
                    capability.schema_versions.size() > kMaxSchemaVersions ||
                    std::find(capability.schema_versions.begin(), capability.schema_versions.end(), 0) !=
                        capability.schema_versions.end();
            })) {
            return {false, "invalid structured capability configuration"};
        }

        try {
            auto tls = options_.tls ? std::make_unique<TlsContext>(*options_.tls) : nullptr;
            auto listener = options_.listen_port
                ? Socket::Listen(options_.listen_ip, options_.listen_port) : Socket{};
            std::unique_ptr<Discovery> discovery;
            if (options_.discovery) {
                try {
                    discovery = std::make_unique<Discovery>(*options_.discovery, options_.listen_port);
                } catch (IoError) {
                    return {false, "could not join IPv4 multicast on the discovery interface"};
                }
            }

            listener_ = std::move(listener);
            tls_ = std::move(tls);
            discovery_ = std::move(discovery);
            sink_ = std::move(sink);
            stopping_ = false;
            if (const auto& browser = options_.browser) {
                try {
                    browser_ = std::make_unique<browser::Server>(*browser, options_.timeout, stopping_, cancelled_,
                        [this](ProtocolWire& wire, const std::string& peer, const std::function<void()>& enable_cancel) {
                            RunBrowser(wire, peer, enable_cancel);
                        });
                } catch (const std::exception& error) {
                    stopping_ = true;
                    lock.unlock();
                    Stop();
                    return {false, std::string("could not start TextLT HTTPS: ") + error.what()};
                }
            }
            worker_ = std::thread([this] { Run(); });
            if (discovery_) {
                discovery_worker_ = std::thread([this] { discovery_->Run(stopping_, sink_); });
            }
            if (ShouldProbe()) probe_worker_ = std::thread([this] { ProbePeer(); });
        } catch (const TlsError& error) {
            return {false, error.reason};
        } catch (IoError) {
            return {false, "could not bind the TextLT listener to the selected IPv4 address and port"};
        } catch (const std::system_error&) {
            stopping_ = true;
            lock.unlock();
            Stop();
            return {false, "could not start the TextLT workers"};
        }
        if (browser_ && options_.browser->report_port) options_.browser->report_port(browser_->Port());
        return {true, {}};
    }

    void Stop() noexcept override {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        changed_.notify_all();
        {
            std::lock_guard lock(probe_mutex_);
            probe_changed_.notify_one();
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        if (discovery_worker_.joinable()) {
            discovery_worker_.join();
        }
        if (probe_worker_.joinable()) probe_worker_.join();
        if (browser_) {
            browser_.reset();
            if (options_.browser->report_port) options_.browser->report_port(0);
        }
        listener_ = {};
        discovery_.reset();
        tls_.reset();
        sink_ = {};
    }

    ProviderResult Send(OutgoingTransfer transfer) override {
        auto& metadata = transfer.metadata;
        if (!ValidTransferId(transfer.identity.transfer_id)) {
            return {false, "invalid transfer ID"};
        }
        if (const auto* text = std::get_if<TextPayload>(&transfer.payload)) {
            if (metadata.type_id != kTextTypeId || metadata.schema_version || text->utf8.size() > kMaxTextBytes ||
                !ValidUtf8(text->utf8) ||
                (metadata.byte_length && *metadata.byte_length != text->utf8.size())) {
                return {false, "invalid text or byte length"};
            }
            metadata.byte_length = text->utf8.size();
        } else if (const auto* payload = std::get_if<StructuredPayload>(&transfer.payload)) {
            if (!options_.tls || !ValidTypeId(metadata.type_id) || metadata.type_id == kTextTypeId ||
                metadata.type_id == kFileTypeId || !metadata.schema_version || !*metadata.schema_version ||
                !metadata.display_name || !ValidFileName(*metadata.display_name) ||
                payload->bytes.size() > kMaxStructuredBytes ||
                (metadata.byte_length && *metadata.byte_length != payload->bytes.size())) {
                return {false, "invalid structured offer or payload (maximum 1 MiB, trusted TLS required)"};
            }
            metadata.byte_length = payload->bytes.size();
        } else {
            const auto* file = std::get_if<std::unique_ptr<std::istream>>(&transfer.payload);
            if (!options_.tls) return {false, "file transfers require trusted TLS"};
            if (metadata.type_id != kFileTypeId || metadata.schema_version || !file || !*file ||
                !metadata.byte_length || !metadata.display_name || !ValidFileName(*metadata.display_name)) {
                return {false, "invalid file name, stream, or byte length"};
            }
        }
        const auto& peer = transfer.identity.peer;
        if (peer.provider_id != kTextLTProviderId) {
            return {false, "unknown outgoing peer"};
        }

        {
            std::lock_guard lock(mutex_);
            if (stopping_ || busy_) {
                return {false, "provider is stopped or busy"};
            }
            std::optional<PeerEndpoint> endpoint;
            if (peer.peer_id == kTextLTManualPeerId && options_.peer_port) {
                endpoint = PeerEndpoint{options_.peer_ip, options_.peer_port};
            } else if (discovery_ && options_.trusted_discovery_peer_id == peer.peer_id) {
                endpoint = discovery_->FindCompatiblePeer(peer.peer_id);
                if (!endpoint) {
                    return {false, "trusted peer is offline or has an unsupported protocol version"};
                }
            } else {
                return {false, "peer has no configured endpoint or trusted fingerprint"};
            }
            active_ = transfer.identity;
            direction_ = kOutgoing;
            busy_ = true;
            cancelled_ = false;
            outgoing_ = PendingSend{std::move(transfer), std::move(*endpoint)};
        }
        changed_.notify_all();
        return {true, {}};
    }

    ProviderResult Accept(const TransferIdentity& offer,
                          std::unique_ptr<std::ostream> destination) override {
        if (destination) {
            return {false, "TextLT file offers require an atomic destination"};
        }
        return Decide(offer, Reply::Accept, {});
    }

    ProviderResult AcceptFile(const TransferIdentity& offer, FileDestinationFactory destination) override {
        if (!destination) return {false, "file offers require a destination"};
        return Decide(offer, Reply::Accept, {}, std::move(destination));
    }

    ProviderResult Decline(const TransferIdentity& offer, std::string reason) override {
        return Decide(offer, Reply::Decline, std::move(reason));
    }

    ProviderResult Cancel(const TransferIdentity& transfer, TransferDirection direction) override {
        {
            std::lock_guard lock(mutex_);
            if (stopping_ || cancelled_ || !active_ || direction != direction_ ||
                !SameTransfer(*active_, transfer)) {
                return {false, "transfer is not active"};
            }
            cancelled_ = true;
        }
        changed_.notify_all();
        return {true, {}};
    }

private:
    struct PendingSend {
        OutgoingTransfer transfer;
        PeerEndpoint endpoint;
    };

    struct Decision {
        Reply reply;
        std::string reason;
        FileDestinationFactory destination;
    };

    ProviderResult Decide(const TransferIdentity& offer, Reply reply, std::string reason,
                          FileDestinationFactory destination = {}) {
        {
            std::lock_guard lock(mutex_);
            if (stopping_ || cancelled_ || !awaiting_decision_ || decision_ ||
                !active_ || !SameTransfer(*active_, offer)) {
                return {false, "offer is not pending"};
            }
            if (reply == Reply::Accept && pending_file_ != static_cast<bool>(destination)) {
                return {false, "wrong destination for this offer"};
            }
            decision_ = Decision{reply, std::move(reason), std::move(destination)};
        }
        changed_.notify_all();
        return {true, {}};
    }

    Decision WaitForDecision(Deadline deadline, const ProtocolWire& wire, bool browser) {
        std::unique_lock lock(mutex_);
        const auto decided = [this] { return stopping_ || cancelled_ || decision_; };
        if (browser) {
            while (!decided() && Clock::now() < deadline) {
                const auto next_poll = Clock::now() + std::chrono::milliseconds(100);
                changed_.wait_until(lock, (std::min)(deadline, next_poll), decided);
                if (!decided() && wire.Disconnected()) {
                    cancelled_ = true;
                    break;
                }
            }
        } else {
            changed_.wait_until(lock, deadline, decided);
        }
        awaiting_decision_ = false;
        if (stopping_ || cancelled_) {
            throw IoError::Cancelled;
        }
        if (!decision_) {
            throw IoError::Timeout;
        }
        return std::move(*decision_);
    }

    void Progress(const TransferIdentity& id, TransferDirection direction,
                  std::uint64_t bytes, std::uint64_t total) {
        sink_(TransferEvent{id, direction, TransferState::InProgress, bytes, total, {}});
    }

    void SendFile(Wire& wire, const OutgoingTransfer& transfer) {
        auto& source = *std::get<std::unique_ptr<std::istream>>(transfer.payload);
        const auto length = *transfer.metadata.byte_length;
        std::array<char, kFileChunkSize> buffer;
        auto next_report = Clock::now();
        for (std::uint64_t bytes = 0; bytes < length;) {
            wire.CheckActive();
            const auto count = static_cast<std::streamsize>(
                std::min<std::uint64_t>(buffer.size(), length - bytes));
            if (!source.read(buffer.data(), count)) throw FileError{"file ended before its advertised size"};
            wire.Write({buffer.data(), static_cast<std::size_t>(count)});
            wire.RenewDeadline(options_.timeout);
            bytes += count;
            if (Clock::now() >= next_report || bytes == length) {
                Progress(transfer.identity, kOutgoing, bytes, length);
                next_report = Clock::now() + std::chrono::milliseconds(100);
            }
        }
        if (source.peek() != std::char_traits<char>::eof() || source.bad() || !source.eof()) {
            throw FileError{"source file changed size or could not be read"};
        }
        wire.SendReply(Reply::EndOfFile);
    }

    std::string ReceiveFile(ProtocolWire& wire, const IncomingOffer& offer, FileDestination& destination) {
        const auto length = *offer.metadata.byte_length;
        auto next_report = Clock::now();
        auto& stream = destination.Stream();
        for (std::uint64_t bytes = 0; bytes < length;) {
            const auto data = wire.Read(static_cast<std::size_t>(
                std::min<std::uint64_t>(kFileChunkSize, length - bytes)));
            if (!stream.write(data.data(), static_cast<std::streamsize>(data.size()))) {
                throw FileError{"could not write to the Downloads staging file"};
            }
            wire.RenewDeadline(options_.timeout);
            bytes += data.size();
            if (Clock::now() >= next_report || bytes == length) {
                Progress(offer.identity, kIncoming, bytes, length);
                next_report = Clock::now() + std::chrono::milliseconds(100);
            }
        }
        if (wire.ReceiveReply() != Reply::EndOfFile) throw ProtocolError::Invalid;
        wire.CheckActive();
        ProviderResult result;
        try {
            result = destination.Commit();
        } catch (const std::exception& error) {
            throw FileError{error.what()};
        }
        if (!result.accepted) throw FileError{std::move(result.reason)};
        return std::move(result.reason);
    }

    Outcome WriteTransfer(const PendingSend& send) {
        const auto& transfer = send.transfer;
        const auto bytes = *transfer.metadata.byte_length;
        const bool file = transfer.metadata.type_id == kFileTypeId;
        const auto* structured = std::get_if<StructuredPayload>(&transfer.payload);
        auto socket = Socket::Open();
        Wire wire(socket, Clock::now() + options_.timeout, stopping_, cancelled_);
        wire.Connect(send.endpoint.ip, send.endpoint.port);
        wire.Authenticate(tls_.get(), false);
        wire.SendHello(structured ? kStructuredProtocolVersion :
                       file ? kFileProtocolVersion : kTextProtocolVersion);
        if (structured) {
            const char command = static_cast<char>(StructuredCommand::Offer);
            wire.Write({&command, 1});
            wire.SendStructuredOffer(transfer.identity.transfer_id, transfer.metadata);
        } else if (file) {
            const char command = static_cast<char>(FileCommand::Offer);
            wire.Write({&command, 1});
            wire.SendFileOffer({transfer.identity.transfer_id, *transfer.metadata.display_name, bytes});
        } else {
            wire.SendOffer({transfer.identity.transfer_id, static_cast<std::uint32_t>(bytes)});
        }
        const auto decision = wire.ReceiveReply();
        if (structured) {
            if (auto failure = ContentFailure(decision)) return *failure;
        }
        if (decision == Reply::Decline) {
            return {TransferState::Declined, "receiver declined the offer"};
        }
        if (file && decision == Reply::FileFailure) throw FileError{"receiver could not stage the file"};
        if (decision != Reply::Accept) throw ProtocolError::Invalid;
        if (file) {
            wire.RenewDeadline(options_.timeout);
            Progress(transfer.identity, kOutgoing, 0, bytes);
            SendFile(wire, transfer);
        } else if (structured) {
            if (!structured->bytes.empty()) {
                wire.Write({reinterpret_cast<const char*>(structured->bytes.data()), structured->bytes.size()});
            }
        } else {
            wire.Write(std::get<TextPayload>(transfer.payload).utf8);
        }
        const auto receipt = wire.ReceiveReply();
        if (structured) {
            if (auto failure = ContentFailure(receipt)) return *failure;
        }
        if (file && receipt == Reply::FileFailure) throw FileError{"receiver could not save the file"};
        if (receipt != Reply::Receipt) throw ProtocolError::Invalid;
        return {TransferState::Completed};
    }

    Outcome ReadTransfer(ProtocolWire& wire, Deadline deadline, const std::string& peer_id,
                         bool browser, std::optional<IncomingOffer>& offer) {
        const auto protocol = wire.ReceiveHello();
        const bool file = protocol == kFileProtocolVersion;
        const bool structured = protocol == kStructuredProtocolVersion;
        std::string transfer_id;
        TransferMetadata metadata;
        if (structured) {
            const auto command = static_cast<StructuredCommand>(wire.Read(1).front());
            if (command == StructuredCommand::Capabilities) {
                wire.SendCapabilities(Capabilities());
                return {TransferState::Completed};
            }
            if (command != StructuredCommand::Offer) throw ProtocolError::Invalid;
            std::tie(transfer_id, metadata) = wire.ReceiveStructuredOffer();
            const auto& capabilities = options_.structured_capabilities;
            const auto type = std::find_if(capabilities.begin(), capabilities.end(),
                [&](const Capability& capability) { return capability.type_id == metadata.type_id; });
            const auto reject = type == capabilities.end() ? Reply::UnknownType :
                std::find(type->schema_versions.begin(), type->schema_versions.end(), *metadata.schema_version)
                    == type->schema_versions.end() ? Reply::UnsupportedSchema : Reply::Ready;
            if (auto failure = ContentFailure(reject)) {
                wire.SendReply(reject);
                return *failure;
            }
        } else if (file) {
            const auto command = static_cast<FileCommand>(wire.Read(1).front());
            if (command == FileCommand::Probe) return {TransferState::Completed};
            if (command != FileCommand::Offer) throw ProtocolError::Invalid;
            auto header = wire.ReceiveFileOffer();
            transfer_id = std::move(header.transfer_id);
            metadata = {kFileTypeId, {}, std::move(header.name), header.length};
        } else {
            auto header = wire.ReceiveOffer();
            transfer_id = std::move(header.transfer_id);
            metadata = TextMetadata(header.length);
        }
        offer = IncomingOffer{
            {{kTextLTProviderId, peer_id}, std::move(transfer_id)}, std::move(metadata)
        };
        {
            std::lock_guard lock(mutex_);
            active_ = offer->identity;
            direction_ = kIncoming;
            pending_file_ = file;
            awaiting_decision_ = true;
        }
        if (browser) sink_(Peer{offer->identity.peer, "Web Browser (unverified)", false, {}});
        sink_(*offer);
        auto decision = WaitForDecision(deadline, wire, browser);
        if (decision.reply == Reply::Decline) {
            wire.SendReply(decision.reply);
            return {TransferState::Declined, std::move(decision.reason)};
        }
        if (file) {
            wire.RenewDeadline(options_.timeout);
            std::string saved;
            try {
                std::unique_ptr<FileDestination> destination;
                try {
                    destination = decision.destination();
                } catch (const std::exception& error) {
                    throw FileError{error.what()};
                }
                if (!destination) throw FileError{"could not open a Downloads staging file"};
                wire.SendReply(Reply::Accept);
                Progress(offer->identity, kIncoming, 0, *offer->metadata.byte_length);
                saved = ReceiveFile(wire, *offer, *destination);
            } catch (const FileError&) {
                TryReply(wire, Reply::FileFailure);
                throw;
            }
            // Publishing is the receiver's completion point. The sender still requires its receipt.
            TryReply(wire, Reply::Receipt);
            return {TransferState::Completed, std::move(saved)};
        }

        wire.SendReply(Reply::Accept);
        if (browser) wire.RenewDeadline(options_.timeout);
        auto text = wire.Read(static_cast<std::size_t>(*offer->metadata.byte_length));
        if (structured) {
            StructuredPayload payload{{text.begin(), text.end()}};
            bool valid = false;
            try {
                valid = options_.validate_structured(offer->metadata, payload);
            } catch (const std::exception&) {
                // A failed handler must never acknowledge an invalid snapshot.
            }
            if (!valid) {
                wire.SendReply(Reply::InvalidPayload);
                return *ContentFailure(Reply::InvalidPayload);
            }
            sink_(ReceivedContent{offer->identity, offer->metadata, std::move(payload)});
            wire.SendReply(Reply::Receipt);
            return {TransferState::Completed};
        }
        if (!ValidUtf8(text)) {
            wire.SendReply(Reply::InvalidText);
            return FromProtocolError(ProtocolError::Invalid);
        }
        sink_(ReceivedContent{offer->identity, offer->metadata, TextPayload{std::move(text)}});
        if (browser) TryReply(wire, Reply::Receipt);
        else wire.SendReply(Reply::Receipt);
        return {TransferState::Completed};
    }

    static void TryReply(ProtocolWire& wire, Reply reply) {
        try { wire.SendReply(reply); }
        catch (IoError) {}
        catch (const TlsError&) {}
        catch (const localsend::Error&) {}
        catch (const localsend::Cancelled&) {}
    }

    void Finish() {
        std::lock_guard lock(mutex_);
        active_.reset();
        decision_.reset();
        awaiting_decision_ = false;
        busy_ = false;
        changed_.notify_all();
    }

    void Report(const TransferIdentity& identity, TransferDirection direction,
                std::uint64_t total, Outcome outcome) {
        const auto completed_bytes = outcome.state == TransferState::Completed
            ? std::optional<std::uint64_t>{total} : std::nullopt;
        sink_(TransferEvent{identity, direction, outcome.state, completed_bytes,
                            total, std::move(outcome.reason), outcome.failure});
    }

    void RunOutgoing(const PendingSend& send) {
        const auto& transfer = send.transfer;
        Outcome outcome{TransferState::Cancelled};
        try {
            if (!stopping_ && !cancelled_) {
                outcome = WriteTransfer(send);
            }
        } catch (IoError error) {
            outcome = FromIoError(error);
        } catch (ProtocolError error) {
            outcome = FromProtocolError(error);
        } catch (const TlsError& error) {
            outcome = {TransferState::Failed, error.reason, error.failure};
        } catch (const FileError& error) {
            outcome = {TransferState::Failed, error.reason, TransferFailure::FileFailure};
        } catch (const std::ios_base::failure& error) {
            outcome = {TransferState::Failed, error.what(), TransferFailure::FileFailure};
        }
        Finish();
        Report(transfer.identity, kOutgoing, *transfer.metadata.byte_length, std::move(outcome));
    }

    template<class Read>
    void RunIncoming(Read read, bool report_connection_failure) {
        std::optional<IncomingOffer> offer;
        Outcome outcome{TransferState::Cancelled};
        try {
            outcome = read(offer);
        } catch (IoError error) {
            outcome = FromIoError(error);
        } catch (ProtocolError error) {
            outcome = FromProtocolError(error);
        } catch (const TlsError& error) {
            outcome = {TransferState::Failed, error.reason, error.failure};
        } catch (const FileError& error) {
            outcome = {TransferState::Failed, error.reason, TransferFailure::FileFailure};
        } catch (const std::ios_base::failure& error) {
            outcome = {TransferState::Failed, error.what(), TransferFailure::FileFailure};
        } catch (const localsend::Cancelled&) {
            outcome = {TransferState::Cancelled};
        } catch (const localsend::Error& error) {
            outcome = {TransferState::Failed, error.reason, error.code};
        } catch (const std::exception& error) {
            outcome = {TransferState::Failed, error.what(), TransferFailure::ConnectionFailure};
        }
        Finish();
        if (offer) {
            Report(offer->identity, kIncoming, *offer->metadata.byte_length, std::move(outcome));
        } else if (report_connection_failure && outcome.failure && outcome.reason) {
            sink_(IncomingConnectionFailure{kTextLTProviderId, *outcome.failure,
                                            std::move(*outcome.reason)});
        }
    }

    void RunBrowser(ProtocolWire& wire, const std::string& peer, const std::function<void()>& enable_cancel) {
        {
            std::unique_lock lock(mutex_);
            if (stopping_) return;
            if (busy_) {
                lock.unlock();
                wire.SendReply(Reply::Decline);
                return;
            }
            busy_ = true;
            cancelled_ = false;
            enable_cancel();
        }
        RunIncoming([&](auto& offer) {
            return ReadTransfer(wire, Clock::now() + options_.timeout, peer, true, offer);
        }, false);
    }

    void ReportManualPeer() {
        if (options_.peer_port && !stopping_) {
            sink_(Peer{{kTextLTProviderId, kTextLTManualPeerId}, "Manual TextLT peer",
                       false, {{kTextTypeId, {}}}});
        }
    }

    bool ShouldProbe() const {
        return options_.probe_manual_peer && tls_ &&
            (options_.peer_port || (discovery_ && options_.trusted_discovery_peer_id));
    }

    std::vector<Capability> ProbeCapabilities(const PeerEndpoint& endpoint, std::uint8_t version,
                                               const std::atomic<bool>& no_cancel) {
        auto socket = Socket::Open();
        Wire wire(socket, Clock::now() + std::chrono::seconds(1), stopping_, no_cancel);
        wire.Connect(endpoint.ip, endpoint.port);
        wire.Authenticate(tls_.get(), false);
        wire.SendHello(version);
        const char query = 0;
        wire.Write({&query, 1});
        if (version == kStructuredProtocolVersion) return wire.ReceiveCapabilities();
        return {{kTextTypeId, {}}, {kFileTypeId, {}}};
    }

    void ProbePeer() {
        ReportManualPeer();
        const std::atomic<bool> no_cancel{false};
        const auto peer_id = options_.peer_port ? kTextLTManualPeerId : *options_.trusted_discovery_peer_id;
        std::unique_lock lock(probe_mutex_);
        while (!stopping_) {
            lock.unlock();
            const auto endpoint = options_.peer_port
                ? std::optional<PeerEndpoint>{{options_.peer_ip, options_.peer_port}}
                : discovery_->FindCompatiblePeer(peer_id);
            if (endpoint) {
                std::optional<std::vector<Capability>> capabilities;
                try {
                    try {
                        capabilities = ProbeCapabilities(*endpoint, kStructuredProtocolVersion, no_cancel);
                    } catch (ProtocolError error) {
                        if (error != ProtocolError::UnsupportedVersion) throw;
                        capabilities = ProbeCapabilities(*endpoint, kFileProtocolVersion, no_cancel);
                    }
                } catch (ProtocolError error) {
                    if (error == ProtocolError::UnsupportedVersion) capabilities = std::vector<Capability>{{kTextTypeId, {}}};
                } catch (IoError) {
                } catch (const TlsError&) {
                }
                if (capabilities && !stopping_) {
                    sink_(PeerCapabilities{{kTextLTProviderId, peer_id}, std::move(*capabilities)});
                    return;
                }
            }
            lock.lock();
            probe_changed_.wait_for(lock, std::chrono::seconds(2), [this] { return stopping_.load(); });
        }
    }

    void Run() {
        if (!ShouldProbe()) ReportManualPeer();
        for (;;) {
            std::optional<PendingSend> send;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [this] { return stopping_ || outgoing_ || (listener_ && !busy_); });
                if (outgoing_) {
                    send = std::move(outgoing_);
                    outgoing_.reset();
                } else if (stopping_) {
                    return;
                }
            }
            if (send) {
                RunOutgoing(*send);
                continue;
            }

            Socket client;
            try {
                if (listener_.Ready(false, kPollMilliseconds)) {
                    std::lock_guard lock(mutex_);
                    if (!stopping_ && !busy_) {
                        client = listener_.Accept();
                        if (client) {
                            busy_ = true;
                            cancelled_ = false;
                        }
                    }
                }
            } catch (IoError) {
                // The listener remains available after a failed individual accept.
            }
            if (client) {
                RunIncoming([&, socket = std::move(client)](auto& offer) mutable {
                    const auto deadline = Clock::now() + options_.timeout;
                    Wire wire(socket, deadline, stopping_, cancelled_);
                    wire.Authenticate(tls_.get(), true);
                    const std::string peer = tls_
                        ? options_.trusted_discovery_peer_id.value_or(kTextLTManualPeerId)
                        : "incoming-" + std::to_string(++last_incoming_peer_);
                    return ReadTransfer(wire, deadline, peer, false, offer);
                }, static_cast<bool>(tls_));
            }
        }
    }

    const TextLTProviderOptions options_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::atomic<bool> stopping_{true};
    std::atomic<bool> cancelled_{false};
    bool busy_ = false;
    Socket listener_;
    std::unique_ptr<TlsContext> tls_;
    std::unique_ptr<browser::Server> browser_;
    ProviderEventSink sink_;
    std::thread worker_;
    std::unique_ptr<Discovery> discovery_;
    std::thread discovery_worker_;
    std::mutex probe_mutex_;
    std::condition_variable probe_changed_;
    std::thread probe_worker_;
    std::optional<PendingSend> outgoing_;
    std::optional<TransferIdentity> active_;
    TransferDirection direction_ = kOutgoing;
    bool awaiting_decision_ = false;
    bool pending_file_ = false;
    std::optional<Decision> decision_;
    std::uint64_t last_incoming_peer_ = 0;
};

} // namespace

bool IsValidTextLTPeerId(std::string_view peer_id) {
    return detail::ValidDiscoveryId(peer_id);
}

bool IsValidTextLTDeviceName(std::string_view name) {
    return detail::ValidDiscoveryName(name);
}

bool IsValidTextLTAddress(std::string_view address) {
    return detail::IsLanIPv4(std::string(address));
}

std::unique_ptr<IConnectProvider> MakeTextLTProvider(TextLTProviderOptions options) {
    return std::make_unique<TextLTProvider>(std::move(options));
}

} // namespace textlt::connect
