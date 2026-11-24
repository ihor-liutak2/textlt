#include "tls.hpp"

#include <openssl/err.h>
#include <openssl/pem.h>

namespace textlt::connect::detail {
namespace {

[[noreturn]] void ConfigurationError(const char* reason) {
    throw TlsError{TransferFailure::TlsFailure, reason};
}

int HexDigit(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    return -1;
}

std::array<unsigned char, 32> ParseFingerprint(std::string_view text) {
    std::array<unsigned char, 32> fingerprint{};
    const bool separated = text.size() == 95;
    if (!separated && text.size() != 64) {
        ConfigurationError("expected peer fingerprint must contain 32 SHA-256 bytes in hex");
    }
    for (std::size_t i = 0; i < fingerprint.size(); ++i) {
        const auto offset = i * (separated ? 3 : 2);
        const int high = HexDigit(text[offset]);
        const int low = HexDigit(text[offset + 1]);
        if (high < 0 || low < 0 || (separated && i != 0 && text[offset - 1] != ':')) {
            ConfigurationError("invalid SHA-256 fingerprint");
        }
        fingerprint[i] = static_cast<unsigned char>(high * 16 + low);
    }
    return fingerprint;
}

// Use the existing nonblocking socket operations and their SIGPIPE handling.
int BioRead(BIO* bio, char* buffer, int size) noexcept {
    BIO_clear_retry_flags(bio);
    try {
        const int result = static_cast<const Socket*>(BIO_get_data(bio))->Read(buffer, size);
        if (result < 0) {
            BIO_set_retry_read(bio);
        }
        return result;
    } catch (IoError) {
        return -1;
    }
}

int BioWrite(BIO* bio, const char* buffer, int size) noexcept {
    BIO_clear_retry_flags(bio);
    try {
        const int result = static_cast<const Socket*>(BIO_get_data(bio))->Write(buffer, size);
        if (result < 0) {
            BIO_set_retry_write(bio);
        }
        return result;
    } catch (IoError) {
        return -1;
    }
}

long BioControl(BIO*, int command, long, void*) noexcept {
    return command == BIO_CTRL_FLUSH ? 1 : 0;
}

} // namespace

TlsContext::TlsContext(const TextLTTlsOptions& options)
    : fingerprint_(ParseFingerprint(options.expected_peer_fingerprint)),
      bio_method_(BIO_meth_new(BIO_TYPE_SOURCE_SINK, "TextLT socket"), BIO_meth_free),
      context_(SSL_CTX_new(TLS_method()), SSL_CTX_free) {
    if (!bio_method_ || !context_ ||
        BIO_meth_set_read(bio_method_.get(), BioRead) != 1 ||
        BIO_meth_set_write(bio_method_.get(), BioWrite) != 1 ||
        BIO_meth_set_ctrl(bio_method_.get(), BioControl) != 1 ||
        SSL_CTX_set_min_proto_version(context_.get(), TLS1_3_VERSION) != 1 ||
        SSL_CTX_set_num_tickets(context_.get(), 0) != 1) {
        ConfigurationError("could not configure TLS 1.3");
    }
    SSL_CTX_set_default_passwd_cb(context_.get(), [](char*, int, int, void*) { return 0; });
    if (SSL_CTX_use_certificate_chain_file(context_.get(), options.certificate_pem.c_str()) != 1 ||
        SSL_CTX_use_PrivateKey_file(context_.get(), options.private_key_pem.c_str(),
                                    SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(context_.get()) != 1) {
        ConfigurationError("could not load the TLS certificate and matching PEM private key");
    }
    SSL_CTX_set_session_cache_mode(context_.get(), SSL_SESS_CACHE_OFF);
    SSL_CTX_set_verify(context_.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    SSL_CTX_set_cert_verify_callback(context_.get(), Verify, this);
}

int TlsContext::Verify(X509_STORE_CTX* store, void* data) noexcept {
    const auto& expected = static_cast<const TlsContext*>(data)->fingerprint_;
    X509* certificate = X509_STORE_CTX_get0_cert(store);
    std::array<unsigned char, 32> actual{};
    unsigned int length = 0;
    int result = X509_V_OK;
    if (!certificate || X509_digest(certificate, EVP_sha256(), actual.data(), &length) != 1 ||
        length != actual.size() || CRYPTO_memcmp(actual.data(), expected.data(), actual.size()) != 0) {
        result = X509_V_ERR_APPLICATION_VERIFICATION;
    } else if (X509_cmp_current_time(X509_get0_notBefore(certificate)) >= 0) {
        result = X509_V_ERR_CERT_NOT_YET_VALID;
    } else if (X509_cmp_current_time(X509_get0_notAfter(certificate)) <= 0) {
        result = X509_V_ERR_CERT_HAS_EXPIRED;
    }
    X509_STORE_CTX_set_error(store, result);
    return result == X509_V_OK;
}

TlsSession TlsContext::Open(Socket& socket, bool incoming) const {
    TlsSession ssl(SSL_new(context_.get()), SSL_free);
    BIO* bio = ssl ? BIO_new(bio_method_.get()) : nullptr;
    if (!bio) {
        ConfigurationError("could not initialize TLS session");
    }
    BIO_set_data(bio, &socket);
    BIO_set_init(bio, 1);
    SSL_set_bio(ssl.get(), bio, bio);
    if (incoming) {
        SSL_set_accept_state(ssl.get());
    } else {
        SSL_set_connect_state(ssl.get());
    }
    return ssl;
}

void ThrowTlsError(SSL* ssl, int error, bool authenticating) {
    const auto verification = SSL_get_verify_result(ssl);
    if (verification != X509_V_OK) {
        const char* reason = verification == X509_V_ERR_APPLICATION_VERIFICATION
            ? "peer certificate fingerprint does not match the configured fingerprint"
            : X509_verify_cert_error_string(verification);
        throw TlsError{TransferFailure::PeerAuthenticationFailed, reason};
    }

    const auto code = ERR_peek_last_error();
    const auto reason_code = ERR_GET_LIB(code) == ERR_LIB_SSL ? ERR_GET_REASON(code) : 0;
    if (reason_code == SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE ||
        reason_code == SSL_R_TLSV13_ALERT_CERTIFICATE_REQUIRED ||
        reason_code == SSL_R_SSLV3_ALERT_BAD_CERTIFICATE ||
        reason_code == SSL_R_SSLV3_ALERT_CERTIFICATE_UNKNOWN ||
        reason_code == SSL_R_TLSV1_ALERT_UNKNOWN_CA ||
        reason_code == SSL_R_SSLV3_ALERT_CERTIFICATE_EXPIRED) {
        const char* reason = reason_code == SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE
            ? "peer did not present a certificate" : "peer rejected the TLS certificate";
        throw TlsError{TransferFailure::PeerAuthenticationFailed, reason};
    }
    if (!authenticating && (error == SSL_ERROR_SYSCALL || error == SSL_ERROR_ZERO_RETURN ||
                            reason_code == SSL_R_UNEXPECTED_EOF_WHILE_READING)) {
        throw IoError::Connection;
    }
    const char* reason = ERR_reason_error_string(code);
    throw TlsError{TransferFailure::TlsFailure, reason ? reason : "TLS connection failed or closed"};
}

} // namespace textlt::connect::detail

namespace textlt::connect {

std::optional<std::string> TextLTCertificateFingerprint(const std::string& certificate_pem) {
    const std::unique_ptr<BIO, decltype(&BIO_free)> input(
        BIO_new_file(certificate_pem.c_str(), "rb"), BIO_free);
    const std::unique_ptr<X509, decltype(&X509_free)> certificate(
        input ? PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr) : nullptr, X509_free);
    std::array<unsigned char, 32> digest{};
    unsigned int length = 0;
    if (!certificate || X509_digest(certificate.get(), EVP_sha256(), digest.data(), &length) != 1 ||
        length != digest.size()) {
        return std::nullopt;
    }

    constexpr char digits[] = "0123456789ABCDEF";
    std::string fingerprint;
    for (const auto byte : digest) {
        if (!fingerprint.empty()) fingerprint += ':';
        fingerprint += digits[byte >> 4];
        fingerprint += digits[byte & 15];
    }
    return fingerprint;
}

} // namespace textlt::connect
