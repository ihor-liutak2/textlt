#pragma once

#include <functional>
#include <ostream>

#include "textlt/connect/types.hpp"

namespace textlt::connect {

struct ProviderResult {
    // A successful request was accepted for processing. Transfer outcomes arrive as events.
    bool accepted = false;
    std::string reason;
};

using ProviderEventSink = std::function<void(ProviderEvent)>;

// A destination stages incoming bytes and publishes them atomically on Commit.
class FileDestination {
public:
    virtual ~FileDestination() = default;
    virtual std::ostream& Stream() = 0;
    virtual ProviderResult Commit() = 0;
};

using FileDestinationFactory = std::function<std::unique_ptr<FileDestination>()>;

class IConnectProvider {
public:
    virtual ~IConnectProvider() = default;

    // The ID is stable for the lifetime of the provider.
    virtual std::string Id() const = 0;
    virtual std::vector<Capability> Capabilities() const = 0;

    // Starts discovery and receiving. The sink may be called from provider threads.
    // A rejected start releases the sink before returning.
    virtual ProviderResult Start(ProviderEventSink sink) = 0;
    // Closes listeners and finishes or cancels work before returning. After Stop
    // returns, no further calls to the sink from that Start are allowed.
    virtual void Stop() noexcept = 0;

    // The caller supplies an unused outgoing identity and transfers ownership of
    // the payload, including any open file stream.
    virtual ProviderResult Send(OutgoingTransfer transfer) = 0;
    // Text and structured offers pass nullptr. Providers that support writing to
    // a plain stream can take ownership here. TextLT file offers use AcceptFile.
    virtual ProviderResult Accept(const TransferIdentity& offer,
                                  std::unique_ptr<std::ostream> file_destination) = 0;
    // The provider calls the factory on its worker and acknowledges only after Commit.
    // An uncommitted destination removes staged bytes when destroyed.
    virtual ProviderResult AcceptFile(const TransferIdentity&, FileDestinationFactory) {
        return {false, "file transfers are unsupported"};
    }
    virtual ProviderResult Decline(const TransferIdentity& offer,
                                   std::string reason) = 0;
    virtual ProviderResult RetryWithPin(const TransferIdentity&, std::string) {
        return {false, "this provider does not use a PIN"};
    }
    virtual ProviderResult Cancel(const TransferIdentity& transfer,
                                  TransferDirection direction) = 0;
};

} // namespace textlt::connect
