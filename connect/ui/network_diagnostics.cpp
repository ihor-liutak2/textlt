#include "network_diagnostics.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string_view>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/utsname.h>
#ifdef __APPLE__
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#endif

#include "json_utils.hpp"
#include "remote/remote_command_runner.hpp"

namespace textlt::connect_ui {
namespace {

std::string Lower(std::string value) {
    for (auto& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

bool IPv4Eligible(std::string_view address) {
    const auto separator = address.find('.');
    if (separator == std::string_view::npos || separator == 0 || separator > 3) return false;
    unsigned first = 0;
    for (const char ch : address.substr(0, separator)) {
        if (ch < '0' || ch > '9') return false;
        first = first * 10 + (ch - '0');
    }
    return first > 0 && first != 127 && first < 224;
}

std::string FormatAddress(int family, const void* data) {
    std::array<char, INET6_ADDRSTRLEN> buffer{};
#ifdef _WIN32
    const auto* result = InetNtopA(family, const_cast<void*>(data), buffer.data(), buffer.size());
#else
    const auto* result = inet_ntop(family, data, buffer.data(), buffer.size());
#endif
    return result ? buffer.data() : "";
}

std::string AddressText(const sockaddr* address) {
    if (!address) return {};
    if (address->sa_family == AF_INET) {
        return FormatAddress(AF_INET, &reinterpret_cast<const sockaddr_in*>(address)->sin_addr);
    }
    if (address->sa_family == AF_INET6) {
        return FormatAddress(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr);
    }
    return {};
}

#ifdef _WIN32
std::string InterfaceType(ULONG type) {
    switch (type) {
    case IF_TYPE_ETHERNET_CSMACD: return "Ethernet";
    case IF_TYPE_IEEE80211: return "Wi-Fi";
    case IF_TYPE_PPP: return "PPP";
    case IF_TYPE_TUNNEL: return "Tunnel";
    case IF_TYPE_SOFTWARE_LOOPBACK: return "Loopback";
    default: return "Unknown";
    }
}

std::string Status(IF_OPER_STATUS status) {
    switch (status) {
    case IfOperStatusUp: return "Up";
    case IfOperStatusDown: return "Down";
    case IfOperStatusTesting: return "Testing";
    case IfOperStatusDormant: return "Dormant";
    case IfOperStatusNotPresent: return "Not present";
    case IfOperStatusLowerLayerDown: return "Lower layer down";
    default: return "Unknown";
    }
}

std::string Origin(IP_PREFIX_ORIGIN origin) {
    switch (origin) {
    case IpPrefixOriginManual: return "Manual";
    case IpPrefixOriginWellKnown: return "Well-known";
    case IpPrefixOriginDhcp: return "DHCP";
    case IpPrefixOriginRouterAdvertisement: return "Router advertisement";
    default: return "Unknown";
    }
}

std::string Utf8(const wchar_t* value) {
    if (!value) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), size, nullptr, nullptr);
    out.pop_back();
    return out;
}

void InspectInterfaces(NetworkEnvironment& environment) {
    ULONG size = 15000;
    constexpr ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_ALL_INTERFACES |
                            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    for (int tries = 0; tries < 3; ++tries) {
        std::vector<std::max_align_t> storage((size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
        auto* first = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
        const auto result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, first, &size);
        if (result == ERROR_BUFFER_OVERFLOW) continue;
        if (result != NO_ERROR) break;
        for (auto* iface = first; iface; iface = iface->Next) {
            auto name = Utf8(iface->FriendlyName);
            if (name.empty() && iface->AdapterName) name = iface->AdapterName;
            auto type = InterfaceType(iface->IfType);
            if (iface->IfType == IF_TYPE_ETHERNET_CSMACD || type == "Unknown") {
                MIB_IF_ROW2 link{};
                link.InterfaceLuid = iface->Luid;
                if (GetIfEntry2(&link) == NO_ERROR && !link.InterfaceAndOperStatusFlags.HardwareInterface) {
                    type = type == "Ethernet" ? "Virtual Ethernet" : "Virtual";
                }
            }
            for (auto* a = iface->FirstUnicastAddress; a; a = a->Next) {
                auto ip = AddressText(a->Address.lpSockaddr);
                if (ip.empty()) continue;
                const bool ipv4 = a->Address.lpSockaddr->sa_family == AF_INET;
                const bool usable = ipv4 && IPv4Eligible(ip) && iface->OperStatus == IfOperStatusUp &&
                    iface->IfType != IF_TYPE_SOFTWARE_LOOPBACK && a->DadState == IpDadStatePreferred;
                auto status = Status(iface->OperStatus);
                if (a->DadState == IpDadStateTentative) status += " / Tentative";
                if (a->DadState == IpDadStateDuplicate) status += " / Duplicate";
                if (a->DadState == IpDadStateDeprecated) status += " / Deprecated";
                const int prefix = a->OnLinkPrefixLength <= (ipv4 ? 32 : 128) ? a->OnLinkPrefixLength : -1;
                environment.addresses.push_back({name, std::move(ip), prefix, std::move(status),
                                                 Origin(a->PrefixOrigin), type, ipv4, usable});
            }
            for (auto* gateway = iface->FirstGatewayAddress; gateway; gateway = gateway->Next) {
                auto ip = AddressText(gateway->Address.lpSockaddr);
                if (!ip.empty()) environment.gateways.push_back({name, std::move(ip)});
            }
        }
        return;
    }
    environment.notice = "Windows interface enumeration unavailable";
}
#else

int PrefixLength(const sockaddr* mask, int family) {
    if (!mask) return -1;
    const auto* bytes = family == AF_INET
        ? reinterpret_cast<const unsigned char*>(&reinterpret_cast<const sockaddr_in*>(mask)->sin_addr)
        : reinterpret_cast<const unsigned char*>(&reinterpret_cast<const sockaddr_in6*>(mask)->sin6_addr);
    auto size = family == AF_INET ? sizeof(in_addr) : sizeof(in6_addr);
#ifdef __APPLE__
    if (family == AF_INET6) {
        constexpr auto offset = offsetof(sockaddr_in6, sin6_addr);
        size = mask->sa_len > offset ? std::min(size, mask->sa_len - offset) : 0;
    }
#endif
    int count = 0;
    bool zero_seen = false;
    for (std::size_t index = 0; index < size; ++index) {
        for (int bit = 7; bit >= 0; --bit) {
            if (bytes[index] & (1 << bit)) {
                if (zero_seen) return -1;
                ++count;
            } else zero_seen = true;
        }
    }
    return count;
}

std::string PosixInterfaceType(const ifaddrs& iface) {
    if (iface.ifa_flags & IFF_LOOPBACK) return "Loopback";
#if defined(__linux__)
    const auto path = std::filesystem::path("/sys/class/net") / iface.ifa_name;
    std::error_code error;
    if (std::filesystem::exists(path / "wireless", error) ||
        std::filesystem::exists(path / "phy80211", error)) return "Wi-Fi";
    if (std::filesystem::exists(path / "tun_flags", error)) return "Tunnel (TUN/TAP)";
    unsigned link = 0;
    if (std::ifstream(path / "type") >> link) {
        if (link == 512) return "PPP";
        if (link == 768 || link == 769 || link == 776 || link == 778) return "Tunnel";
        const auto target = std::filesystem::read_symlink(path, error).generic_string();
        const bool virtual_device = !error && target.find("/virtual/") != std::string::npos;
        if (link == 1) return virtual_device ? "Virtual Ethernet" : "Ethernet";
        if (virtual_device) return "Virtual";
    }
#elif defined(__APPLE__)
    if (iface.ifa_addr && iface.ifa_addr->sa_family == AF_LINK) {
        switch (reinterpret_cast<const sockaddr_dl*>(iface.ifa_addr)->sdl_type) {
        case IFT_ETHER: {
            ifmediareq media{};
            strlcpy(media.ifm_name, iface.ifa_name, sizeof(media.ifm_name));
            const int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (socket_fd >= 0) {
                const bool wifi = ioctl(socket_fd, SIOCGIFMEDIA, &media) == 0 &&
                    IFM_TYPE(media.ifm_current) == IFM_IEEE80211;
                close(socket_fd);
                if (wifi) return "Wi-Fi";
            }
            return "Ethernet";
        }
        case IFT_PPP: return "PPP";
        case IFT_GIF:
        case IFT_STF: return "Tunnel";
        case IFT_BRIDGE: return "Bridge";
        case IFT_L2VLAN: return "VLAN";
        default: break;
        }
    }
    if (iface.ifa_flags & IFF_POINTOPOINT) return "Point-to-point";
#endif
    return "Unknown";
}

void InspectInterfaces(NetworkEnvironment& environment) {
    ifaddrs* first = nullptr;
    if (getifaddrs(&first) != 0) {
        environment.notice = "Interface enumeration unavailable";
        return;
    }
    std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> entries(first, freeifaddrs);
    std::map<std::string, std::string> types;
    for (auto* iface = first; iface; iface = iface->ifa_next) {
        const auto type = PosixInterfaceType(*iface);
        if (type != "Unknown") types.try_emplace(iface->ifa_name, type);
    }
    for (auto* iface = first; iface; iface = iface->ifa_next) {
        auto ip = AddressText(iface->ifa_addr);
        if (ip.empty()) continue;
        const int family = iface->ifa_addr->sa_family;
        const bool ipv4 = family == AF_INET;
        const bool up = iface->ifa_flags & IFF_UP;
        const bool running = iface->ifa_flags & IFF_RUNNING;
        const bool usable = ipv4 && IPv4Eligible(ip) && up && running && !(iface->ifa_flags & IFF_LOOPBACK);
        const auto type = types.find(iface->ifa_name);
        environment.addresses.push_back({iface->ifa_name, std::move(ip),
            PrefixLength(iface->ifa_netmask, family), !up ? "Down" : running ? "Up" : "Up / No link", "Unknown",
            type == types.end() ? "Unknown" : type->second, ipv4, usable});
    }
}
#endif

#if defined(__linux__)
void InspectLinuxGateways(NetworkEnvironment& environment) {
    std::ifstream route("/proc/net/route");
    std::string line;
    std::getline(route, line);
    while (std::getline(route, line)) {
        std::istringstream row(line);
        std::string iface;
        std::uint32_t destination, gateway, flags;
        if (row >> iface >> std::hex >> destination >> gateway >> flags;
            row && destination == 0 && (flags & 1) != 0) {
            environment.gateways.push_back({iface, gateway ? FormatAddress(AF_INET, &gateway) : "On-link (IPv4)"});
        }
    }
    std::ifstream routes6("/proc/net/ipv6_route");
    while (std::getline(routes6, line)) {
        std::istringstream row(line);
        std::string destination, source, gateway, iface;
        unsigned prefix, source_prefix, metric, reference, use, flags;
        if (!(row >> destination >> std::hex >> prefix >> source >> source_prefix >> gateway >>
              metric >> reference >> use >> flags >> iface) ||
            prefix != 0 || source_prefix != 0 || !(flags & 1) || (flags & 0x200) ||
            gateway.size() != 32) continue;
        std::array<unsigned char, 16> address{};
        for (std::size_t index = 0; index < address.size(); ++index) {
            address[index] = static_cast<unsigned char>(
                std::strtoul(gateway.substr(index * 2, 2).c_str(), nullptr, 16));
        }
        environment.gateways.push_back({iface, gateway == std::string(32, '0')
            ? "On-link (IPv6)" : FormatAddress(AF_INET6, address.data())});
    }
}
#endif

#if defined(__APPLE__)
void InspectMacGateways(NetworkEnvironment& environment, const std::atomic<bool>& cancelled) {
    for (const char* family : {"-inet", "-inet6"}) {
        const auto result = RemoteCommandRunner().RunStreaming(
            {"/sbin/route", "-n", "get", family, "default"}, {}, &cancelled, {}, {2});
        if (result.exit_code != 0) continue;
        std::istringstream lines(result.output);
        std::string key, value, gateway, iface;
        for (std::string line; std::getline(lines, line);) {
            std::istringstream row(line);
            if (!(row >> key >> value)) continue;
            if (key == "gateway:") gateway = value;
            if (key == "interface:") iface = value;
        }
        if (!gateway.empty()) environment.gateways.push_back({iface, gateway});
    }
}
#endif

} // namespace

NetworkDiagnostics InspectLocalNetwork() {
    NetworkDiagnostics result;
#ifdef _WIN32
    result.local.name = "Native Windows";
#elif defined(__APPLE__)
    result.local.name = "macOS";
#else
    utsname version{};
    const bool microsoft = uname(&version) == 0 &&
        (Lower(version.release).find("microsoft") != std::string::npos ||
         Lower(version.version).find("microsoft") != std::string::npos);
    result.wsl = microsoft || std::getenv("WSL_INTEROP") ||
        std::getenv("WSL_DISTRO_NAME");
    result.local.name = result.wsl ? "Linux guest (WSL)" : "Native Linux";
    if (result.wsl) {
        result.host.name = "Windows host";
        result.host.notice = "Reading host interfaces…";
        result.wsl_mode = "Unknown";
    }
#endif
    InspectInterfaces(result.local);
#if defined(__linux__)
    InspectLinuxGateways(result.local);
#endif
    return result;
}

void InspectSupplementalNetwork(NetworkDiagnostics& diagnostics, const std::atomic<bool>& cancelled) {
#if defined(__linux__)
    if (!diagnostics.wsl || cancelled) return;
    auto mode = RemoteCommandRunner().RunStreaming(
        {"wslinfo", "--networking-mode"}, {}, &cancelled, {}, {2});
    if (mode.exit_code == 0) {
        std::istringstream output(mode.output);
        std::string value;
        if (output >> value) {
            const auto lower = Lower(value);
            if (lower == "nat" || lower == "mirrored" || lower == "bridged" || lower == "none" ||
                lower == "virtioproxy" || lower == "consomme") diagnostics.wsl_mode = value;
        }
    }
    if (cancelled) return;
    constexpr char script[] = R"PS(
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$addresses = @()
$gateways = @()
foreach ($config in @(Get-NetIPConfiguration -All)) {
    $adapter = $config.NetAdapter
    $type = if ($adapter -and $adapter.InterfaceType -eq 24) { 'Loopback' }
        elseif ($adapter -and $adapter.InterfaceType -eq 131) { 'Tunnel' }
        elseif ($adapter -and $adapter.InterfaceType -eq 23) { 'PPP' }
        elseif ($adapter -and ($adapter.InterfaceType -eq 71 -or
            $adapter.NdisPhysicalMedium -match '802.11|Wireless|^(1|9)$')) { 'Wi-Fi' }
        elseif ($adapter -and $adapter.Virtual) { 'Virtual' }
        elseif ($adapter -and $adapter.InterfaceType -eq 6) { 'Ethernet' }
        elseif ($adapter -and $adapter.MediaType) { [string]$adapter.MediaType }
        else { 'Unknown' }
    foreach ($addr in @(Get-NetIPAddress -InterfaceIndex $config.InterfaceIndex -ErrorAction SilentlyContinue)) {
        $addresses += [PSCustomObject]@{
            name = [string]$config.InterfaceAlias; ip = [string]$addr.IPAddress
            prefix = [int]$addr.PrefixLength; ipv4 = $addr.AddressFamily -in @('IPv4', 2)
            status = if ($adapter) { [string]$adapter.Status + ' / ' + [string]$addr.AddressState }
                else { [string]$addr.AddressState }
            origin = [string]$addr.PrefixOrigin; type = $type
        }
    }
    foreach ($route in @($config.IPv4DefaultGateway) + @($config.IPv6DefaultGateway)) {
        if ($route -and $route.NextHop) {
            $gateways += [PSCustomObject]@{ name = [string]$config.InterfaceAlias; ip = [string]$route.NextHop }
        }
    }
}
[PSCustomObject]@{ addresses = $addresses; gateways = $gateways } | ConvertTo-Json -Depth 4 -Compress
)PS";
    const RemoteCommandRunner runner;
    auto result = runner.RunStreaming(
        {"powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script},
        {}, &cancelled, {}, {8});
    if (result.exit_code == 127 && !cancelled) {
        constexpr char system_powershell[] =
            "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe";
        std::error_code error;
        if (std::filesystem::exists(system_powershell, error)) {
            result = runner.RunStreaming(
                {system_powershell, "-NoProfile", "-NonInteractive", "-Command", script},
                {}, &cancelled, {}, {8});
        }
    }
    diagnostics.host.notice = "Windows host data unavailable through WSL interop";
    if (result.exit_code != 0) return;
    auto json = Json::parse(result.output, nullptr, false);
    if (!json.is_object()) return;
    const auto addresses = json.find("addresses");
    const auto gateways = json.find("gateways");
    if (addresses == json.end() || !addresses->is_array() ||
        gateways == json.end() || !gateways->is_array()) return;
    for (const auto& item : *addresses) {
        if (!item.is_object() || !item.contains("ip") || !item["ip"].is_string()) continue;
        const auto address = item.value("ip", "");
        const bool ipv4 = item.value("ipv4", false);
        const auto guest = std::find_if(diagnostics.local.addresses.begin(), diagnostics.local.addresses.end(),
            [&](const auto& a) { return a.ipv4 == ipv4 && a.address == address; });
        const bool host_only = guest == diagnostics.local.addresses.end();
        diagnostics.host.addresses.push_back({item.value("name", ""), address,
            item.value("prefix", -1), item.value("status", ""), item.value("origin", ""),
            item.value("type", ""), ipv4, !host_only && guest->usable, host_only});
    }
    for (const auto& item : *gateways) {
        if (!item.is_object()) continue;
        diagnostics.host.gateways.push_back({item.value("name", ""), item.value("ip", "")});
    }
    diagnostics.host.notice.clear();
#elif defined(__APPLE__)
    InspectMacGateways(diagnostics.local, cancelled);
#else
    (void)diagnostics;
    (void)cancelled;
#endif
}

bool IsAutomaticLocalSendAddress(const std::string& address) {
    return address.empty() || address == "0.0.0.0" || Lower(address) == "auto";
}

std::string SelectLanIPv4(const NetworkEnvironment& environment) {
    const InterfaceAddress* best = nullptr;
    int best_score = -1;
    for (const auto& address : environment.addresses) {
        if (!address.usable || !address.ipv4 || address.host_only) continue;
        const bool gateway = std::any_of(environment.gateways.begin(), environment.gateways.end(),
            [&](const auto& item) {
                return item.interface_name == address.interface_name &&
                    (IPv4Eligible(item.address) || item.address == "On-link (IPv4)");
            });
        const bool physical_lan = address.type == "Wi-Fi" || address.type == "Ethernet";
        const int score = (address.address.compare(0, 8, "169.254.") == 0 ? 0 : 8) +
            (physical_lan ? 3 : 0) + (gateway ? 2 : 0);
        if (score > best_score) {
            best = &address;
            best_score = score;
        }
    }
    return best ? best->address : "";
}

} // namespace textlt::connect_ui
