#pragma once

#include "socket.hpp"
#include "textlt/connect/textlt_provider.hpp"

#include <openssl/ssl.h>

#include <array>

namespace textlt::connect::detail {

struct TlsError {
    TransferFailure failure;
    std::string reason;
};

using TlsSession = std::unique_ptr<SSL, decltype(&SSL_free)>;

class TlsContext {
public:
    explicit TlsContext(const TextLTTlsOptions& options);
    TlsSession Open(Socket& socket, bool incoming) const;

private:
    static int Verify(X509_STORE_CTX* store, void* data) noexcept;

    std::array<unsigned char, 32> fingerprint_{};
    std::unique_ptr<BIO_METHOD, decltype(&BIO_meth_free)> bio_method_{nullptr, BIO_meth_free};
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context_{nullptr, SSL_CTX_free};
};

[[noreturn]] void ThrowTlsError(SSL* ssl, int error, bool authenticating);

} // namespace textlt::connect::detail
