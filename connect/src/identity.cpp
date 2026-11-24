#include "identity.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>

#include <openssl/pem.h>
#include <openssl/rand.h>

namespace textlt::connect::detail {
namespace {

namespace fs = std::filesystem;

[[noreturn]] void IdentityError() {
    throw std::runtime_error("could not create the identity PEM");
}

std::string RandomSuffix() {
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) IdentityError();
    constexpr char digits[] = "0123456789abcdef";
    std::string suffix;
    for (const auto byte : bytes) {
        suffix += digits[byte >> 4];
        suffix += digits[byte & 15];
    }
    return suffix;
}

void GenerateIdentity(const fs::path& path, const char* common_name) {
    std::error_code error;
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path(), error);
    if (error) IdentityError();
    const auto staging = path.parent_path() / (".textlt-identity-" + RandomSuffix());
    if (!fs::create_directory(staging, error)) IdentityError();
    struct Cleanup {
        fs::path directory;
        ~Cleanup() {
            std::error_code ignored;
            fs::remove(directory / "identity.pem", ignored);
            fs::remove(directory, ignored);
        }
    } cleanup{staging};
#ifndef _WIN32
    fs::permissions(staging, fs::perms::owner_all, error);
    if (error) IdentityError();
#endif
    const auto temporary = staging / "identity.pem";
#ifdef _WIN32
    FILE* file = nullptr;
    if (_wfopen_s(&file, temporary.c_str(), L"wb") != 0) IdentityError();
#else
    auto* file = std::fopen(temporary.c_str(), "wb");
    if (!file) IdentityError();
#endif
    const auto close_file = [](FILE* stream) { std::fclose(stream); };
    std::unique_ptr<FILE, decltype(close_file)> output(file, close_file);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
        EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free);
    std::unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(), X509_free);
    if (!key || !certificate) IdentityError();
    auto* cert = certificate.get();
    auto* name = X509_get_subject_name(cert);
    if (X509_set_version(cert, 2) != 1 ||
        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) != 1 ||
        !X509_gmtime_adj(X509_getm_notBefore(cert), -3600) ||
        !X509_gmtime_adj(X509_getm_notAfter(cert), 315360000L) ||
        X509_set_pubkey(cert, key.get()) != 1 ||
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>(common_name), -1, -1, 0) != 1 ||
        X509_set_issuer_name(cert, name) != 1 ||
        !X509_sign(cert, key.get(), EVP_sha256()) ||
        PEM_write_X509(file, cert) != 1 ||
        PEM_write_PrivateKey(file, key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1 ||
        std::fflush(file) != 0) IdentityError();
    if (std::fclose(output.release()) != 0) IdentityError();
#ifndef _WIN32
    fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write, error);
    if (error) IdentityError();
#endif
    fs::create_hard_link(temporary, path, error);
    if (error && error != std::errc::file_exists) IdentityError();
}

} // namespace

bool EnsureIdentityFile(const fs::path& path, const char* common_name) {
    if (path.empty()) return false;
    try {
        if (!fs::exists(path)) GenerateIdentity(path, common_name);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace textlt::connect::detail
