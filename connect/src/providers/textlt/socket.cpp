#include "socket.hpp"

#include <utility>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace textlt::connect::detail {
namespace {

constexpr char kDiscoveryGroup[] = "239.255.76.84";
constexpr std::uint16_t kDiscoveryPort = 47653;

#ifdef _WIN32
using AddressLength = int;

bool Retry() {
    const int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
}

bool Connecting() {
    return WSAGetLastError() == WSAEWOULDBLOCK;
}

void InitializeSockets() {
    struct Runtime {
        int status;
        Runtime() {
            WSADATA data{};
            status = WSAStartup(MAKEWORD(2, 2), &data);
        }
        ~Runtime() {
            if (status == 0) {
                WSACleanup();
            }
        }
    };
    static const Runtime runtime;
    if (runtime.status != 0) {
        throw IoError::Connection;
    }
}
#else
using AddressLength = socklen_t;

bool Retry() {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

bool Connecting() {
    return errno == EINPROGRESS || errno == EINTR;
}
#endif

sockaddr_in Endpoint(const std::string& ip, std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) {
        throw IoError::Connection;
    }
    address.sin_port = htons(port);
    return address;
}

} // namespace

bool IsUnicastIPv4(const std::string& ip) {
    in_addr address{};
    if (ip.find('\0') != std::string::npos || inet_pton(AF_INET, ip.c_str(), &address) != 1) {
        return false;
    }
    const auto first_octet = ntohl(address.s_addr) >> 24;
    return first_octet > 0 && first_octet < 224;
}

bool IsLanIPv4(const std::string& ip) {
    return IsUnicastIPv4(ip) && ip.compare(0, 4, "127.") != 0;
}

Socket::Socket(Handle handle) : handle_(handle) {
    if (!*this) {
        throw IoError::Connection;
    }

#ifdef _WIN32
    unsigned long nonblocking = 1;
    const bool configured = ioctlsocket(handle_, FIONBIO, &nonblocking) == 0;
#else
    const bool configured = fcntl(handle_, F_SETFL, O_NONBLOCK) == 0 &&
                            fcntl(handle_, F_SETFD, FD_CLOEXEC) == 0;
#endif
    if (!configured) {
        Close();
        throw IoError::Connection;
    }

#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (setsockopt(handle_, SOL_SOCKET, SO_NOSIGPIPE,
                   reinterpret_cast<const char*>(&enabled), sizeof(enabled)) != 0) {
        Close();
        throw IoError::Connection;
    }
#endif
}

Socket::~Socket() {
    Close();
}

Socket::Socket(Socket&& other) noexcept : handle_(std::exchange(other.handle_, Invalid)) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        Close();
        handle_ = std::exchange(other.handle_, Invalid);
    }
    return *this;
}

void Socket::Close() noexcept {
    if (!*this) {
        return;
    }
#ifdef _WIN32
    closesocket(handle_);
#else
    close(handle_);
#endif
    handle_ = Invalid;
}

Socket Socket::Open() {
#ifdef _WIN32
    InitializeSockets();
#endif
    return Socket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
}

Socket Socket::JoinMulticast(const std::string& interface_ip) {
#ifdef _WIN32
    InitializeSockets();
    using MulticastOption = unsigned long;
#else
    using MulticastOption = unsigned char;
#endif
    Socket socket_handle(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    const int reuse = 1;
#ifdef IP_MULTICAST_ALL
    const int joined_groups_only = 0;
#endif
    const auto any = Endpoint("0.0.0.0", kDiscoveryPort);
    const auto group = Endpoint(kDiscoveryGroup, kDiscoveryPort);
    const auto interface = Endpoint(interface_ip, 0);
    const ip_mreq membership{group.sin_addr, interface.sin_addr};
    const MulticastOption enabled = 1;
    const auto set = [&](int level, int option, const auto& value) {
        return setsockopt(socket_handle.handle_, level, option,
                          reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
    };
    if (!set(SOL_SOCKET, SO_REUSEADDR, reuse) ||
#ifdef SO_REUSEPORT
        !set(SOL_SOCKET, SO_REUSEPORT, reuse) ||
#endif
        bind(socket_handle.handle_, reinterpret_cast<const sockaddr*>(&any), sizeof(any)) != 0 ||
#ifdef IP_MULTICAST_ALL
        !set(IPPROTO_IP, IP_MULTICAST_ALL, joined_groups_only) ||
#endif
        !set(IPPROTO_IP, IP_ADD_MEMBERSHIP, membership) ||
        !set(IPPROTO_IP, IP_MULTICAST_IF, interface.sin_addr) ||
        !set(IPPROTO_IP, IP_MULTICAST_TTL, enabled) ||
        !set(IPPROTO_IP, IP_MULTICAST_LOOP, enabled)) {
        throw IoError::Connection;
    }
    return socket_handle;
}

void Socket::Announce(std::string_view bytes) const {
    const auto group = Endpoint(kDiscoveryGroup, kDiscoveryPort);
    sendto(handle_, bytes.data(), static_cast<int>(bytes.size()), 0,
           reinterpret_cast<const sockaddr*>(&group), sizeof(group));
}

int Socket::ReceiveAnnouncement(char* buffer, int size, std::string& source_ip) const {
    sockaddr_in source{};
    AddressLength source_length = sizeof(source);
    const auto received = recvfrom(handle_, buffer, size, 0, reinterpret_cast<sockaddr*>(&source),
                                   &source_length);
    if (received <= 0) {
        return 0;
    }
    char ip[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &source.sin_addr, ip, sizeof(ip))) {
        return 0;
    }
    source_ip = ip;
    return static_cast<int>(received);
}

Socket Socket::Listen(const std::string& ip, std::uint16_t port) {
    auto listener = Open();
#ifdef _WIN32
    constexpr int option = SO_EXCLUSIVEADDRUSE;
#else
    constexpr int option = SO_REUSEADDR;
#endif
    const int enabled = 1;
    const auto address = Endpoint(ip, port);
    if (setsockopt(listener.handle_, SOL_SOCKET, option,
                   reinterpret_cast<const char*>(&enabled), sizeof(enabled)) != 0 ||
        bind(listener.handle_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener.handle_, 4) != 0) {
        throw IoError::Connection;
    }
    return listener;
}

bool Socket::BeginConnect(const std::string& ip, std::uint16_t port) {
    const auto address = Endpoint(ip, port);
    if (::connect(handle_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
        return true;
    }
    if (!Connecting()) {
        throw IoError::Connection;
    }
    return false;
}

void Socket::FinishConnect() const {
    int error = 0;
    AddressLength length = sizeof(error);
    if (getsockopt(handle_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0 ||
        error != 0) {
        throw IoError::Connection;
    }
}

Socket Socket::Accept() const {
    const auto client = accept(handle_, nullptr, nullptr);
    if (client == Invalid && Retry()) {
        return {};
    }
    return Socket(client);
}

bool Socket::Ready(bool writing, int milliseconds) const {
#ifdef _WIN32
    fd_set ready, errors;
    FD_ZERO(&ready);
    FD_ZERO(&errors);
    FD_SET(handle_, &ready);
    FD_SET(handle_, &errors);
    timeval timeout{0, milliseconds * 1000};
    const int result = select(0, writing ? nullptr : &ready, writing ? &ready : nullptr,
                              &errors, &timeout);
#else
    pollfd descriptor{handle_, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
    const int result = poll(&descriptor, 1, milliseconds);
#endif
    if (result < 0 && !Retry()) {
        throw IoError::Connection;
    }
    return result > 0;
}

int Socket::Read(char* buffer, int size) const {
    const auto result = recv(handle_, buffer, size, 0);
    if (result < 0 && Retry()) {
        return -1;
    }
    if (result <= 0) {
        throw IoError::Connection;
    }
    return static_cast<int>(result);
}

int Socket::Write(const char* buffer, int size) const {
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    const auto result = send(handle_, buffer, size, flags);
    if (result < 0 && Retry()) {
        return -1;
    }
    if (result <= 0) {
        throw IoError::Connection;
    }
    return static_cast<int>(result);
}

} // namespace textlt::connect::detail
