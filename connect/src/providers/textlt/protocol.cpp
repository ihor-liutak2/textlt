#include "protocol.hpp"

#include <openssl/err.h>

#include <algorithm>
#include <utility>

namespace textlt::connect::detail {
namespace {

constexpr std::string_view kMagic = "TLTC";


} // namespace

bool ValidTransferId(std::string_view id) {
    return !id.empty() && id.size() <= kMaxTransferId &&
           std::all_of(id.begin(), id.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
           });
}

bool ValidUtf8(std::string_view text) {
    std::uint32_t code_point = 0;
    std::uint32_t minimum = 0;
    int remaining = 0;

    for (unsigned char byte : text) {
        if (remaining != 0) {
            if ((byte & 0xc0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (byte & 0x3f);
            if (--remaining == 0 &&
                (code_point < minimum || code_point > 0x10ffff ||
                 (code_point >= 0xd800 && code_point <= 0xdfff))) {
                return false;
            }
        } else if (byte <= 0x7f) {
            continue;
        } else if (byte >= 0xc2 && byte <= 0xdf) {
            code_point = byte & 0x1f;
            minimum = 0x80;
            remaining = 1;
        } else if (byte >= 0xe0 && byte <= 0xef) {
            code_point = byte & 0x0f;
            minimum = 0x800;
            remaining = 2;
        } else if (byte >= 0xf0 && byte <= 0xf4) {
            code_point = byte & 0x07;
            minimum = 0x10000;
            remaining = 3;
        } else {
            return false;
        }
    }
    return remaining == 0;
}

Wire::Wire(Socket& socket, Deadline deadline, const std::atomic<bool>& stopping,
           const std::atomic<bool>& cancelled)
    : socket_(socket), deadline_(deadline), stopping_(stopping), cancelled_(cancelled) {}

int Wire::PollTimeout() const {
    if (stopping_ || cancelled_) {
        throw IoError::Cancelled;
    }
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline_ - Clock::now());
    if (remaining.count() <= 0) {
        throw IoError::Timeout;
    }
    return static_cast<int>(std::min<long long>(remaining.count(), kPollMilliseconds));
}

void Wire::Wait(bool writing) const {
    while (!socket_.Ready(writing, PollTimeout())) {}
}

template <typename Operation>
int Wire::RunTls(Operation operation, bool authenticating) const {
    for (;;) {
        PollTimeout();
        ERR_clear_error();
        const int result = operation(tls_.get());
        if (result > 0) {
            return result;
        }
        const int error = SSL_get_error(tls_.get(), result);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
            ThrowTlsError(tls_.get(), error, authenticating);
        }
        Wait(error == SSL_ERROR_WANT_WRITE);
    }
}

void Wire::Connect(const std::string& ip, std::uint16_t port) const {
    if (!socket_.BeginConnect(ip, port)) {
        Wait(true);
        socket_.FinishConnect();
    }
}

void Wire::Authenticate(const TlsContext* context, bool incoming) {
    if (context) {
        tls_ = context->Open(socket_, incoming);
        RunTls(SSL_do_handshake, true);
        // With TLS 1.3 the client may finish before the server verifies its certificate.
        if (incoming) {
            SendReply(Reply::Ready);
        } else if (ReceiveReply() != Reply::Ready) {
            throw ProtocolError::Invalid;
        }
    }
}

void Wire::SendHello(std::uint8_t version) const {
    auto hello = std::string(kMagic);
    hello.push_back(static_cast<char>(version));
    Write(hello);
    const auto reply = ReceiveReply();
    if (reply == Reply::UnsupportedVersion) {
        throw ProtocolError::UnsupportedVersion;
    }
    if (reply != Reply::Ready) {
        throw ProtocolError::Invalid;
    }
}

std::uint8_t Wire::ReceiveHello() const {
    const auto hello = Read(kMagic.size() + 1);
    if (std::string_view(hello).substr(0, kMagic.size()) != kMagic) {
        throw ProtocolError::Invalid;
    }
    const auto version = static_cast<std::uint8_t>(hello.back());
    if (version != kTextProtocolVersion && (!tls_ || (version != kFileProtocolVersion && version != kStructuredProtocolVersion))) {
        SendReply(Reply::UnsupportedVersion);
        throw ProtocolError::UnsupportedVersion;
    }
    SendReply(Reply::Ready);
    return version;
}

bool ValidTypeId(std::string_view type) {
    return !type.empty() && type.size() <= 64 &&
        std::all_of(type.begin(), type.end(), [](unsigned char byte) {
            return (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
                   byte == '.' || byte == '-' || byte == '_';
        });
}

namespace {

void AppendWord(std::string& data, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) data.push_back(static_cast<char>(value >> shift));
}

std::uint32_t ReadWord(std::string_view data) {
    std::uint32_t value = 0;
    for (unsigned char byte : data) value = (value << 8) | byte;
    return value;
}

} // namespace

void Wire::SendStructuredOffer(const std::string& id, const TransferMetadata& metadata) const {
    const auto& name = *metadata.display_name;
    std::string header{static_cast<char>(id.size()), static_cast<char>(metadata.type_id.size()),
                       static_cast<char>(name.size())};
    AppendWord(header, *metadata.schema_version);
    AppendWord(header, static_cast<std::uint32_t>(*metadata.byte_length));
    Write(header);
    Write(id);
    Write(metadata.type_id);
    Write(name);
}

std::pair<std::string, TransferMetadata> Wire::ReceiveStructuredOffer() const {
    const auto header = Read(11);
    const auto id_size = static_cast<unsigned char>(header[0]);
    const auto type_size = static_cast<unsigned char>(header[1]);
    const auto name_size = static_cast<unsigned char>(header[2]);
    const auto version = ReadWord(std::string_view(header).substr(3, 4));
    const auto length = ReadWord(std::string_view(header).substr(7, 4));
    if (!id_size || id_size > kMaxTransferId || !type_size || type_size > 64 ||
        !version || length > kMaxStructuredBytes) throw ProtocolError::Invalid;
    auto id = Read(id_size);
    auto type = Read(type_size);
    auto name = Read(name_size);
    if (!ValidTransferId(id) || !ValidTypeId(type) || !ValidFileName(name)) throw ProtocolError::Invalid;
    return {std::move(id), {std::move(type), version, std::move(name), length}};
}

void Wire::SendCapabilities(const std::vector<Capability>& capabilities) const {
    std::string data(1, static_cast<char>(capabilities.size()));
    for (const auto& capability : capabilities) {
        data.push_back(static_cast<char>(capability.type_id.size()));
        data.push_back(static_cast<char>(capability.schema_versions.size()));
        data += capability.type_id;
        for (const auto version : capability.schema_versions) AppendWord(data, version);
    }
    Write(data);
}

std::vector<Capability> Wire::ReceiveCapabilities() const {
    const auto count = static_cast<unsigned char>(Read(1).front());
    if (count > kMaxCapabilities) throw ProtocolError::Invalid;
    std::vector<Capability> capabilities;
    for (unsigned i = 0; i < count; ++i) {
        const auto header = Read(2);
        const auto size = static_cast<unsigned char>(header[0]);
        const auto versions = static_cast<unsigned char>(header[1]);
        if (!size || size > 64 || versions > kMaxSchemaVersions) throw ProtocolError::Invalid;
        Capability capability{Read(size), {}};
        if (!ValidTypeId(capability.type_id)) throw ProtocolError::Invalid;
        for (unsigned j = 0; j < versions; ++j) {
            const auto version = ReadWord(Read(4));
            if (!version) throw ProtocolError::Invalid;
            capability.schema_versions.push_back(version);
        }
        capabilities.push_back(std::move(capability));
    }
    return capabilities;
}

bool ValidFileName(std::string_view name) {
    return !name.empty() && name.size() <= kMaxFileName && ValidUtf8(name) &&
           std::none_of(name.begin(), name.end(), [](unsigned char byte) {
               return byte < 32 || byte == 127;
           });
}

void Wire::SendFileOffer(const FileOffer& offer) const {
    char header[10] = {static_cast<char>(offer.transfer_id.size()),
                       static_cast<char>(offer.name.size())};
    for (int i = 0; i < 8; ++i) header[i + 2] = static_cast<char>(offer.length >> (56 - 8 * i));
    Write({header, sizeof(header)});
    Write(offer.transfer_id);
    Write(offer.name);
}

FileOffer Wire::ReceiveFileOffer() const {
    const auto header = Read(10);
    const auto id_size = static_cast<unsigned char>(header[0]);
    const auto name_size = static_cast<unsigned char>(header[1]);
    if (!id_size || id_size > kMaxTransferId || !name_size) throw ProtocolError::Invalid;
    std::uint64_t length = 0;
    for (std::size_t i = 2; i < header.size(); ++i) {
        length = (length << 8) | static_cast<unsigned char>(header[i]);
    }
    auto id = Read(id_size);
    auto name = Read(name_size);
    if (!ValidTransferId(id) || !ValidFileName(name)) throw ProtocolError::Invalid;
    return {std::move(id), std::move(name), length};
}

void Wire::SendOffer(const TextOffer& offer) const {
    const char header[] = {
        static_cast<char>(offer.transfer_id.size()), static_cast<char>(offer.length >> 24),
        static_cast<char>(offer.length >> 16), static_cast<char>(offer.length >> 8),
        static_cast<char>(offer.length)
    };
    Write({header, sizeof(header)});
    Write(offer.transfer_id);
}

TextOffer Wire::ReceiveOffer() const {
    const auto header = Read(5);
    const auto id_size = static_cast<unsigned char>(header.front());
    std::uint32_t length = 0;
    for (std::size_t i = 1; i < header.size(); ++i) {
        length = (length << 8) | static_cast<unsigned char>(header[i]);
    }
    if (id_size == 0 || id_size > kMaxTransferId || length > kMaxTextBytes) {
        throw ProtocolError::Invalid;
    }
    auto id = Read(id_size);
    if (!ValidTransferId(id)) {
        throw ProtocolError::Invalid;
    }
    return {std::move(id), length};
}

void Wire::Write(std::string_view bytes) const {
    while (!bytes.empty()) {
        int written;
        if (tls_) {
            written = RunTls([bytes](SSL* ssl) {
                return SSL_write(ssl, bytes.data(), static_cast<int>(bytes.size()));
            });
        } else {
            Wait(true);
            written = socket_.Write(bytes.data(), static_cast<int>(bytes.size()));
        }
        if (written > 0) {
            bytes.remove_prefix(written);
        }
    }
}

std::string Wire::Read(std::size_t size) const {
    std::string bytes(size, '\0');
    std::size_t offset = 0;
    while (offset < size) {
        int count;
        if (tls_) {
            count = RunTls([&](SSL* ssl) {
                return SSL_read(ssl, bytes.data() + offset, static_cast<int>(size - offset));
            });
        } else {
            Wait(false);
            count = socket_.Read(bytes.data() + offset, static_cast<int>(size - offset));
        }
        if (count > 0) {
            offset += count;
        }
    }
    return bytes;
}

void Wire::SendReply(Reply reply) const {
    const auto byte = static_cast<char>(reply);
    Write({&byte, 1});
}

Reply Wire::ReceiveReply() const {
    return static_cast<Reply>(Read(1).front());
}

} // namespace textlt::connect::detail
