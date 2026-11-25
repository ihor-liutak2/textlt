#pragma once

#include "tls.hpp"

#include <atomic>
#include <string>
#include <string_view>

namespace textlt::connect::detail {

constexpr std::uint8_t kTextProtocolVersion = 1;
constexpr std::uint8_t kFileProtocolVersion = 2;
constexpr std::uint8_t kStructuredProtocolVersion = 3;
constexpr std::uint32_t kMaxStructuredBytes = 1024 * 1024;
constexpr std::size_t kMaxCapabilities = 64;
constexpr std::size_t kMaxSchemaVersions = 32;
enum class StructuredCommand : char { Capabilities = 0, Offer = 1 };
constexpr std::size_t kMaxFileName = 255;

enum class FileCommand : char { Probe = 0, Offer = 1 };
constexpr std::size_t kMaxTransferId = 64;
constexpr std::size_t kMaxTextBytes = 65536;

enum class Reply : char { Ready = 1, UnsupportedVersion, Accept, Decline, Receipt, InvalidText, FileFailure, EndOfFile, UnknownType, UnsupportedSchema, InvalidPayload };
enum class ProtocolError { Invalid, UnsupportedVersion };

struct TextOffer {
    std::string transfer_id;
    std::uint32_t length;
};

struct FileOffer {
    std::string transfer_id;
    std::string name;
    std::uint64_t length;
};

bool ValidTypeId(std::string_view type);
bool ValidFileName(std::string_view name);
bool ValidTransferId(std::string_view id);
bool ValidUtf8(std::string_view text);

class ProtocolWire {
public:
    ProtocolWire() = default;
    virtual ~ProtocolWire() = default;

    void SendHello(std::uint8_t version = kTextProtocolVersion) const;
    std::uint8_t ReceiveHello() const;
    void SendStructuredOffer(const std::string& id, const TransferMetadata& metadata) const;
    std::pair<std::string, TransferMetadata> ReceiveStructuredOffer() const;
    void SendCapabilities(const std::vector<Capability>& capabilities) const;
    std::vector<Capability> ReceiveCapabilities() const;
    void SendFileOffer(const FileOffer& offer) const;
    FileOffer ReceiveFileOffer() const;
    void SendOffer(const TextOffer& offer) const;
    TextOffer ReceiveOffer() const;
    virtual void RenewDeadline(std::chrono::milliseconds timeout) = 0;
    virtual void CheckActive() const = 0;
    virtual bool Disconnected() const { return false; }
    virtual void Write(std::string_view bytes) const = 0;
    virtual std::string Read(std::size_t size) const = 0;
    void SendReply(Reply reply) const;
    Reply ReceiveReply() const;

protected:
    virtual bool SupportsVersion(std::uint8_t version) const = 0;
};

class Wire final : public ProtocolWire {
public:
    Wire(Socket& socket, Deadline deadline, const std::atomic<bool>& stopping,
         const std::atomic<bool>& cancelled);

    void Connect(const std::string& ip, std::uint16_t port) const;
    void Authenticate(const TlsContext* context, bool incoming);
    void RenewDeadline(std::chrono::milliseconds timeout) override { deadline_ = Clock::now() + timeout; }
    void CheckActive() const override { PollTimeout(); }
    void Write(std::string_view bytes) const override;
    std::string Read(std::size_t size) const override;

private:
    bool SupportsVersion(std::uint8_t version) const override {
        return version == kTextProtocolVersion ||
            (tls_ && (version == kFileProtocolVersion || version == kStructuredProtocolVersion));
    }
    int PollTimeout() const;
    void Wait(bool writing) const;
    template <typename Operation>
    int RunTls(Operation operation, bool authenticating = false) const;

    Socket& socket_;
    TlsSession tls_{nullptr, SSL_free};
    Deadline deadline_;
    const std::atomic<bool>& stopping_;
    const std::atomic<bool>& cancelled_;
};

} // namespace textlt::connect::detail
