#include "https.hpp"

#include "../../identity.hpp"

#include <algorithm>
#include <array>
#include <limits>

#include <openssl/pem.h>
#include <openssl/rand.h>

namespace textlt::connect::localsend {
namespace {

constexpr std::size_t kChunk = 64 * 1024;
constexpr std::size_t kMaxControl = 1024 * 1024;

[[noreturn]] void IdentityError() {
    throw Error{TransferFailure::TlsFailure, "could not load or create the LocalSend identity"};
}

std::optional<std::string> Digest(X509* certificate) {
    if (!certificate) return {};
    std::array<unsigned char, 32> bytes{};
    unsigned int length = 0;
    if (X509_digest(certificate, EVP_sha256(), bytes.data(), &length) != 1 || length != bytes.size()) {
        throw Error{TransferFailure::TlsFailure, "could not read the TLS fingerprint"};
    }
    return Hex(bytes.data(), length);
}

template<class Body>
void SetRequest(http::request<Body>& request, std::string_view target, const tcp::endpoint& endpoint) {
    request.version(11);
    request.method(http::verb::post);
    request.target(std::string(target));
    request.set(http::field::host, endpoint.address().to_string() + ":" + std::to_string(endpoint.port()));
    request.keep_alive(false);
}

} // namespace

std::string Hex(const unsigned char* bytes, std::size_t size) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        result += digits[bytes[i] >> 4];
        result += digits[bytes[i] & 15];
    }
    return result;
}

std::string RandomToken() {
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), bytes.size()) != 1) IdentityError();
    return Hex(bytes.data(), bytes.size());
}

std::optional<std::string> NormalizeFingerprint(std::string_view fingerprint) {
    if (fingerprint.size() != 64) return {};
    std::string result(fingerprint);
    for (auto& ch : result) {
        if (ch >= 'a' && ch <= 'f') ch -= 'a' - 'A';
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) return {};
    }
    return result;
}

std::string LoadIdentity(ssl::context& context, const std::filesystem::path& path) {
    if (!detail::EnsureIdentityFile(path, "TextLT LocalSend")) IdentityError();
    // PEM files contain both the certificate and the corresponding key.
    const auto filename = path.u8string();
    context.use_certificate_chain_file(filename);
    context.use_private_key_file(filename, ssl::context::pem);
    auto* native = context.native_handle();
    if (SSL_CTX_check_private_key(native) != 1 ||
        SSL_CTX_set_min_proto_version(native, TLS1_2_VERSION) != 1) IdentityError();
    SSL_CTX_set_session_cache_mode(native, SSL_SESS_CACHE_OFF);
    SSL_CTX_set_options(native, SSL_OP_NO_TICKET);
    SSL_CTX_set_num_tickets(native, 0);
    // LocalSend certificates are self-signed. The caller checks the actual leaf
    // against the selected peer or binds the incoming request to that leaf.
    context.set_verify_mode(ssl::verify_peer);
    context.set_verify_callback([](bool, ssl::verify_context&) { return true; });
    const auto digest = Digest(SSL_CTX_get0_certificate(native));
    if (!digest) IdentityError();
    return *digest;
}

Https::Https(ssl::context& context, const std::atomic<bool>& stop, const std::atomic<bool>* cancel)
    : stream_(io_, context), stop_(stop), cancel_(cancel) {}

tcp::socket& Https::Socket() { return beast::get_lowest_layer(stream_).socket(); }

void Https::Check() const {
    if (stop_ || (cancel_ && *cancel_)) throw Cancelled{};
}

template<class Initiate>
boost::system::error_code Https::Wait(Milliseconds timeout, Initiate initiate, TransferFailure code) {
    Check();
    bool done = false;
    boost::system::error_code result;
    const auto deadline = Clock::now() + timeout;
    io_.restart();
    initiate([&](boost::system::error_code error, auto...) {
        result = error;
        done = true;
    });
    while (!done && Clock::now() < deadline && !stop_ && (!cancel_ || !*cancel_)) {
        io_.run_for(std::chrono::milliseconds(20));
    }
    if (!done) {
        boost::system::error_code ignored;
        Socket().cancel(ignored);
        io_.restart();
        io_.run();
        Check();
        throw Error{TransferFailure::Timeout, "LocalSend request timed out"};
    }
    if (result && result != http::error::need_buffer && result != http::error::need_more) {
        throw Error{code, result.message()};
    }
    return result;
}

std::string Https::Connect(const tcp::endpoint& address, Milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    Wait(timeout, [&](auto done) { Socket().async_connect(address, done); });
    Wait(std::chrono::duration_cast<Milliseconds>(deadline - Clock::now()),
         [&](auto done) { stream_.async_handshake(ssl::stream_base::client, done); },
         TransferFailure::TlsFailure);
    auto fingerprint = Fingerprint();
    if (!fingerprint) {
        throw Error{TransferFailure::PeerAuthenticationFailed, "LocalSend peer did not present a certificate"};
    }
    return std::move(*fingerprint);
}

void Https::Connect(const tcp::endpoint& address, const std::string& fingerprint, Milliseconds timeout) {
    if (Connect(address, timeout) != fingerprint) {
        throw Error{TransferFailure::PeerAuthenticationFailed, "LocalSend peer certificate fingerprint mismatch"};
    }
}

void Https::Accept(Milliseconds timeout) {
    Wait(timeout, [&](auto done) { stream_.async_handshake(ssl::stream_base::server, done); },
         TransferFailure::TlsFailure);
}

std::optional<std::string> Https::Fingerprint() {
    std::unique_ptr<X509, decltype(&X509_free)> cert(
        SSL_get1_peer_certificate(stream_.native_handle()), X509_free);
    return Digest(cert.get());
}

bool Https::Disconnected() {
    boost::system::error_code error;
    Socket().non_blocking(true, error);
    if (error) return true;
    char byte;
    Socket().receive(net::buffer(&byte, 1), net::socket_base::message_peek, error);
    return error != net::error::would_block && error != net::error::try_again;
}

void Https::ReadHeaders(Headers& request, Milliseconds timeout) {
    request.body_limit((std::numeric_limits<std::uint64_t>::max)());
    Wait(timeout, [&](auto done) { http::async_read_header(stream_, buffer_, request, done); });
}

void Https::ReadBody(Headers& request, Milliseconds timeout,
                     const std::function<void(const char*, std::size_t)>& consume) {
    std::array<char, kChunk> chunk;
    while (!request.is_done()) {
        request.get().body().data = chunk.data();
        request.get().body().size = chunk.size();
        Wait(timeout, [&](auto done) { http::async_read_some(stream_, buffer_, request, done); });
        const auto size = chunk.size() - request.get().body().size;
        if (size) consume(chunk.data(), size);
    }
}

http::response<http::string_body> Https::ReadResponse(Milliseconds timeout) {
    http::response_parser<http::string_body> response;
    response.body_limit(kMaxControl);
    Wait(timeout, [&](auto done) { http::async_read(stream_, buffer_, response, done); });
    return response.release();
}

void Https::Reply(http::status status, std::string body, Milliseconds timeout) {
    http::response<http::string_body> response(status, 11);
    if (status != http::status::continue_) response.keep_alive(false);
    if (!body.empty()) {
        response.set(http::field::content_type, "application/json");
        response.body() = std::move(body);
    }
    if (status != http::status::continue_) response.prepare_payload();
    Wait(timeout, [&](auto done) { http::async_write(stream_, response, done); });
}

void Https::Post(std::string_view target, std::string body, Milliseconds timeout) {
    http::request<http::string_body> request;
    SetRequest(request, target, Socket().remote_endpoint());
    request.set(http::field::content_type, "application/json");
    request.body() = std::move(body);
    request.prepare_payload();
    Wait(timeout, [&](auto done) { http::async_write(stream_, request, done); });
}

void Https::Upload(std::string_view target, std::istream& input, std::uint64_t length,
                   Milliseconds timeout, const std::function<void(std::uint64_t)>& progress) {
    http::request<http::empty_body> request;
    SetRequest(request, target, Socket().remote_endpoint());
    request.set(http::field::content_type, "application/octet-stream");
    request.content_length(length);
    http::request_serializer<http::empty_body> header(request);
    Wait(timeout, [&](auto done) { http::async_write_header(stream_, header, done); });
    std::array<char, kChunk> chunk;
    for (std::uint64_t total = 0; total < length;) {
        Check();
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), length - total));
        if (!input.read(chunk.data(), static_cast<std::streamsize>(count))) {
            throw Error{TransferFailure::FileFailure, "could not read the complete source file"};
        }
        Wait(timeout, [&](auto done) { net::async_write(stream_, net::buffer(chunk.data(), count), done); });
        total += count;
        progress(total);
    }
}

} // namespace textlt::connect::localsend
