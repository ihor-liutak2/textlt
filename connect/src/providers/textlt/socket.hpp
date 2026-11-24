#pragma once

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace textlt::connect::detail {

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
constexpr int kPollMilliseconds = 25;

bool IsUnicastIPv4(const std::string& ip);
bool IsLanIPv4(const std::string& ip);

enum class IoError { Connection, Timeout, Cancelled };

class Socket {
public:
#ifdef _WIN32
    using Handle = SOCKET;
    static constexpr Handle Invalid = INVALID_SOCKET;
#else
    using Handle = int;
    static constexpr Handle Invalid = -1;
#endif

    Socket() = default;
    ~Socket();
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    static Socket Listen(const std::string& ip, std::uint16_t port);
    static Socket Open();
    static Socket JoinMulticast(const std::string& interface_ip);
    void Announce(std::string_view bytes) const;
    int ReceiveAnnouncement(char* buffer, int size, std::string& source_ip) const;
    bool BeginConnect(const std::string& ip, std::uint16_t port);
    void FinishConnect() const;
    Socket Accept() const;
    bool Ready(bool writing, int milliseconds) const;
    // Returns -1 when the operation should be retried.
    int Read(char* buffer, int size) const;
    int Write(const char* buffer, int size) const;

    explicit operator bool() const { return handle_ != Invalid; }

private:
    explicit Socket(Handle handle);
    void Close() noexcept;
    Handle handle_ = Invalid;
};

} // namespace textlt::connect::detail
