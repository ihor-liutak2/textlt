#include "network_diagnostics.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
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
#include <netdb.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>
#ifdef __APPLE__
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <sys/ioctl.h>
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

bool HasIPv4DefaultRoute(const NetworkEnvironment& environment, const InterfaceAddress& address) {
    return std::any_of(environment.gateways.begin(), environment.gateways.end(), [&](const auto& item) {
        return item.interface_name == address.interface_name &&
            (IPv4Eligible(item.address) || item.address == "On-link (IPv4)");
    });
}

bool IsLanAdapter(const InterfaceAddress& address) {
    return address.type == "Wi-Fi" || address.type == "Ethernet";
}

bool IsTunnel(const InterfaceAddress& address) {
    const auto type = Lower(address.type);
    const auto name = Lower(address.interface_name);
    return type.find("tunnel") != std::string::npos || type == "ppp" || type == "point-to-point" ||
        name.find("vpn") != std::string::npos || name.find("wireguard") != std::string::npos ||
        name.find("tailscale") != std::string::npos || name.find("zerotier") != std::string::npos ||
        name.rfind("utun", 0) == 0 || name.rfind("tun", 0) == 0 || name.rfind("tap", 0) == 0 ||
        name.rfind("ppp", 0) == 0 || name.rfind("wg", 0) == 0;
}

bool IsUsefulIPv4(const NetworkEnvironment& environment, const InterfaceAddress& address) {
    return address.ipv4 && IPv4Eligible(address.address) && address.type != "Loopback" &&
        (address.status == "Up" || address.status.rfind("Up / ", 0) == 0) &&
        address.status != "Up / No link" &&
        (address.address.rfind("169.254.", 0) != 0 || Lower(address.origin) == "manual") &&
        !IsTunnel(address) &&
        ((address.type.rfind("Virtual", 0) != 0 && address.type != "Bridge") ||
         HasIPv4DefaultRoute(environment, address));
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

#ifndef _WIN32
PortAvailability BindPortAvailability(bool tcp, unsigned port) {
    const int socket_fd = socket(AF_INET, tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (socket_fd < 0) return PortAvailability::Unknown;
    sockaddr_in address{};
#ifdef __APPLE__
    address.sin_len = sizeof(address);
#endif
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    const int result = bind(socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    const int error = errno;
    close(socket_fd);
    if (result == 0) return PortAvailability::Available;
    return error == EADDRINUSE ? PortAvailability::InUse : PortAvailability::Unknown;
}
#endif

#if defined(__linux__)
PortAvailability LinuxPortAvailability(bool tcp, unsigned port) {
    std::ostringstream hex;
    hex << std::uppercase << std::hex << port;
    const auto suffix_to_match = ":" + hex.str();
    for (const auto* suffix : {"", "6"}) {
        std::ifstream endpoints(std::string("/proc/net/") + (tcp ? "tcp" : "udp") + suffix);
        std::string line;
        if (!std::getline(endpoints, line)) continue;
        while (std::getline(endpoints, line)) {
            std::istringstream entry(line);
            std::string index, local, remote, state;
            if (entry >> index >> local >> remote >> state;
                local.size() >= suffix_to_match.size() &&
                local.compare(local.size() - suffix_to_match.size(), suffix_to_match.size(), suffix_to_match) == 0 &&
                (!tcp || state == "0A")) {
                return PortAvailability::InUse;
            }
        }
    }
    return BindPortAvailability(tcp, port);
}

bool HasDiagnosticPort(std::string_view value) {
    const auto parse = [](std::string_view number) -> std::optional<unsigned> {
        unsigned port = 0;
        const auto end = number.data() + number.size();
        const auto result = std::from_chars(number.data(), end, port);
        if (result.ec != std::errc{} || result.ptr != end) return {};
        return port;
    };
    while (!value.empty()) {
        const auto comma = value.find(',');
        const auto ports = value.substr(0, comma);
        const auto separator = ports.find_first_of(":-");
        const auto first = parse(ports.substr(0, separator));
        const auto last = separator == std::string_view::npos ? first : parse(ports.substr(separator + 1));
        if (first && last && ((*first <= 53317 && *last >= 53317) ||
                              (*first <= 53327 && *last >= 53327))) return true;
        if (comma == std::string_view::npos) break;
        value.remove_prefix(comma + 1);
    }
    return false;
}

bool NftPortsMatch(const Json& ports) {
    if (ports.is_number_integer()) return ports == 53317 || ports == 53327;
    if (!ports.is_object()) return false;
    const auto range = ports.find("range");
    if (range != ports.end() && range->is_array() && range->size() == 2) {
        return HasDiagnosticPort((*range)[0].dump() + "-" + (*range)[1].dump());
    }
    const auto set = ports.find("set");
    return set != ports.end() && set->is_array() && std::any_of(set->begin(), set->end(), NftPortsMatch);
}

void InspectLinuxFirewall(std::vector<std::string>& findings, const std::string& platform,
                          const std::atomic<bool>& cancelled) {
    const RemoteCommandRunner runner;
    const auto nft = runner.RunStreaming({"nft", "-j", "list", "ruleset"}, {}, &cancelled, {}, {3});
    const auto ruleset = Json::parse(nft.output, nullptr, false);
    const auto iptables = runner.RunStreaming({"iptables", "-S"}, {}, &cancelled, {}, {3});
    const bool nft_readable = nft.exit_code == 0 && ruleset.is_object() && ruleset.contains("nftables") &&
                              ruleset["nftables"].is_array();
    bool nft_inbound = false;
    if (nft_readable) {
        nft_inbound = std::any_of(ruleset["nftables"].begin(), ruleset["nftables"].end(), [](const auto& item) {
            return item.is_object() && item.contains("chain") && item["chain"].is_object() &&
                   item["chain"].contains("hook") && item["chain"]["hook"] == "input";
        });
    }
    const bool iptables_inbound = iptables.exit_code == 0 &&
        (iptables.output.find("-P INPUT ACCEPT\n") == std::string::npos ||
         iptables.output.find("-A INPUT ") != std::string::npos);
    const auto prefix = platform + " · ";
    if (nft_inbound || iptables_inbound) {
        findings.push_back(prefix + "The Linux firewall checks incoming traffic. General rules may affect both ports.");
    } else if (nft_readable && iptables.exit_code == 0) {
        findings.push_back(prefix + "The Linux firewall has no incoming rules that block either port.");
    } else if (nft.exit_code == 127 && iptables.exit_code == 127) {
        findings.push_back(prefix + "Linux firewall could not be checked. The tools for reading its rules are missing.");
    } else {
        findings.push_back(prefix + "Linux firewall could not be fully checked. Reading its rules may require administrator access.");
    }
    if (iptables.exit_code == 0) {
        std::istringstream lines(iptables.output);
        for (std::string line; std::getline(lines, line);) {
            std::istringstream tokens(line);
            for (std::string token; tokens >> token;) {
                if (token != "--dport" && token != "--dports" && token != "--sport" && token != "--sports") continue;
                std::string ports;
                if (tokens >> ports; HasDiagnosticPort(ports)) {
                    findings.push_back(prefix + "Linux firewall rule (iptables) " + line);
                    break;
                }
            }
        }
    }
    if (!nft_readable) return;
    for (const auto& entry : ruleset["nftables"]) {
        if (!entry.is_object() || !entry.contains("rule")) continue;
        const auto& rule = entry["rule"];
        if (!rule.is_object() || !rule.contains("expr") || !rule["expr"].is_array()) continue;
        const auto& expressions = rule["expr"];
        const bool matches = std::any_of(expressions.begin(), expressions.end(), [](const auto& expression) {
            if (!expression.is_object() || !expression.contains("match")) return false;
            const auto& match = expression["match"];
            if (!match.is_object() || !match.contains("left") || !match.contains("right")) return false;
            const auto& left = match["left"];
            if (!left.is_object() || !left.contains("payload") || !left["payload"].is_object()) return false;
            const auto field = left["payload"].find("field");
            return field != left["payload"].end() && (*field == "dport" || *field == "sport") &&
                   NftPortsMatch(match["right"]);
        });
        if (matches) {
            findings.push_back(prefix + "Linux firewall rule (nftables) in " +
                rule.value("table", std::string("unknown table")) + "/" +
                rule.value("chain", std::string("unknown chain")) + ". " + expressions.dump());
        }
    }
}

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
    result.container = std::getenv("TEXTLT_CONTAINER") != nullptr ||
        std::ifstream("/.dockerenv").good();
    result.wsl = !result.container &&
        (microsoft || std::getenv("WSL_INTEROP") || std::getenv("WSL_DISTRO_NAME"));
    const char* network_mode = std::getenv("TEXTLT_CONTAINER_NETWORK");
    const bool host_network = network_mode && std::string_view(network_mode) == "host";
    result.local.name = result.container
        ? (host_network ? "Docker (host network)" : "Docker container")
        : (result.wsl ? "Linux guest (WSL)" : "Native Linux");
    if (result.wsl) {
        result.host.name = "Windows host";
        result.host.notice = "Reading host interfaces…";
        result.wsl_mode = "Unknown";
        std::ifstream release("/etc/os-release");
        for (std::string line; std::getline(release, line);) {
            if (line.rfind("NAME=", 0) != 0) continue;
            result.wsl_distribution = line.substr(5);
            auto& name = result.wsl_distribution;
            if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
                name = name.substr(1, name.size() - 2);
            }
            break;
        }
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
    if (cancelled) return;
    if (diagnostics.container) {
        constexpr char host_name[] = "host.docker.internal";
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* addresses = nullptr;
        if (getaddrinfo(host_name, nullptr, &hints, &addresses) != 0) return;
        const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resolved(addresses, freeaddrinfo);
        diagnostics.host.name = "Docker host endpoint";
        for (const addrinfo* address = resolved.get(); address; address = address->ai_next) {
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address->ai_addr);
            const auto ip = FormatAddress(AF_INET, &ipv4->sin_addr);
            if (!IPv4Eligible(ip)) continue;
            diagnostics.host.addresses.push_back(
                {host_name, ip, -1, "Up", "DNS", "Network", true, true, true});
        }
        return;
    }
    if (!diagnostics.wsl) return;
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

void InspectLocalSendPorts(NetworkDiagnostics& diagnostics, const std::atomic<bool>& cancelled) {
    auto& findings = diagnostics.localsend_port_findings;
    findings.clear();
    auto& ports = diagnostics.localsend_ports;
    ports.clear();
#if defined(__linux__)
    const std::string platform = diagnostics.container ? diagnostics.local.name :
        diagnostics.wsl ? "WSL" : "Linux";
    for (const auto port : {53317u, 53327u}) {
        ports.push_back({platform, port, LinuxPortAvailability(true, port), LinuxPortAvailability(false, port)});
    }
    if (diagnostics.container) {
        findings.push_back("Docker port checks cover the container network. TCP availability does not verify LAN discovery.");
        return;
    }
    InspectLinuxFirewall(findings, platform, cancelled);
    if (!diagnostics.wsl || cancelled) return;
#elif !defined(_WIN32)
    const auto system = diagnostics.local.name.empty() ? std::string("This device") : diagnostics.local.name;
    for (const auto port : {53317u, 53327u}) {
        ports.push_back({system, port, BindPortAvailability(true, port), BindPortAvailability(false, port)});
    }
    findings.push_back("Firewall rule details are unavailable on this system.");
    return;
#endif
    for (const auto port : {53317u, 53327u}) ports.push_back({"Windows host", port});
    constexpr char script[] = R"PS(
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$ports = @(53317, 53327)
$tcpListening = @{}
function Test-Bind($protocol, $port) {
    $probe = $null
    try {
        $type = if ($protocol -eq 'TCP') { [Net.Sockets.SocketType]::Stream } else { [Net.Sockets.SocketType]::Dgram }
        $transport = if ($protocol -eq 'TCP') { [Net.Sockets.ProtocolType]::Tcp } else { [Net.Sockets.ProtocolType]::Udp }
        $probe = [Net.Sockets.Socket]::new([Net.Sockets.AddressFamily]::InterNetwork, $type, $transport)
        $probe.ExclusiveAddressUse = $true
        $probe.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Any, $port))
        return 'free'
    } catch {
        if ($_.Exception.GetBaseException().SocketErrorCode -eq 'AddressAlreadyInUse') { return 'in-use' }
        return 'unknown'
    } finally {
        if ($probe) { $probe.Dispose() }
    }
}
foreach ($protocol in @('TCP', 'UDP')) {
    try {
        $endpoints = if ($protocol -eq 'TCP') { Get-NetTCPConnection -State Listen }
            else { Get-NetUDPEndpoint }
        $ranges = & netsh.exe interface ipv4 show excludedportrange "protocol=$($protocol.ToLowerInvariant())"
        $rangesReadable = $LASTEXITCODE -eq 0
        foreach ($port in $ports) {
            $bound = @($endpoints | Where-Object LocalPort -eq $port)
            if ($protocol -eq 'TCP') { $tcpListening[$port] = $bound }
            if ($bound.Count) {
                "@port $protocol $port in-use"
                $owners = foreach ($endpoint in $bound) {
                    $process = Get-Process -Id $endpoint.OwningProcess -ErrorAction SilentlyContinue
                    $name = if ($process) { $process.ProcessName } else { "process $($endpoint.OwningProcess)" }
                    "$name on $($endpoint.LocalAddress)"
                }
                "Windows $protocol port $port is in use by $(($owners | Select-Object -Unique) -join ', ')."
            } else {
                $reserved = $false
                foreach ($range in $ranges) {
                    if ($range -match '^\s*(\d+)\s+(\d+)\s*\*?\s*$' -and
                        [int]$Matches[1] -le $port -and [int]$Matches[2] -ge $port) { $reserved = $true }
                }
                $state = if ($reserved) { 'reserved' } else { Test-Bind $protocol $port }
                "@port $protocol $port $state"
                switch ($state) {
                    'reserved' { "Windows $protocol port $port is reserved by the system." }
                    'free' { "Windows $protocol port $port could be opened." }
                    'in-use' { "Windows $protocol port $port is in use." }
                    default { "Windows could not confirm whether $protocol port $port is free." }
                }
                if (!$rangesReadable) { "Windows could not read system reservations for $protocol port $port." }
            }
        }
    } catch { "Windows $protocol ports 53317 and 53327 could not be checked." }
}
function Has-Port($values, $port) {
    foreach ($item in (@($values) -split ',\s*')) {
        if ($item -eq "$port") { return $true }
        if ($item -match '^(\d+)-(\d+)$' -and
            [int]$Matches[1] -le $port -and [int]$Matches[2] -ge $port) { return $true }
    }
    return $false
}
try {
    $active = @(Get-NetConnectionProfile | ForEach-Object {
        if ($_.NetworkCategory -eq 'DomainAuthenticated') { 'Domain' }
        else { [string]$_.NetworkCategory }
    } | Select-Object -Unique)
    $profiles = @(Get-NetFirewallProfile -PolicyStore ActiveStore | Where-Object { $_.Name -in $active })
    if (!$profiles.Count) { 'Windows firewall profile could not be identified.' }
    foreach ($profile in $profiles) {
        $policy = if ($profile.Enabled -eq 'False') { 'off' }
            elseif ($profile.AllowInboundRules -eq 'False') { 'on and set to block incoming connections' }
            elseif ($profile.DefaultInboundAction -eq 'Allow') { 'on and allows incoming connections by default' }
            else { 'on and blocks incoming connections by default' }
        "Windows firewall on $($profile.Name) networks is $policy."
    }
    $firewall = New-Object -ComObject HNetCfg.FwPolicy2
    $activeMask = $firewall.CurrentProfileTypes
    $rules = @($firewall.Rules | Where-Object { $_.Protocol -in @(6, 17) })
    foreach ($port in $ports) {
        $matching = @($rules | Where-Object {
            (Has-Port $_.LocalPorts $port) -or (Has-Port $_.RemotePorts $port)
        })
        if (!$matching.Count) { "Windows firewall has no TCP or UDP rules that name port $port. General rules may still apply." }
        foreach ($rule in $matching) {
            $protocol = if ($rule.Protocol -eq 6) { 'TCP' } else { 'UDP' }
            $direction = if ($rule.Direction -eq 1) { 'incoming' } else { 'outgoing' }
            $action = if ($rule.Action -eq 1) { 'allows' } else { 'blocks' }
            $state = if (!$rule.Enabled) { 'off' } elseif (!($rule.Profiles -band $activeMask)) { 'only on other network profiles' } else { 'on' }
            "Windows firewall rule '$($rule.Name)'. The rule is $state. It $action $direction $protocol. Local ports $($rule.LocalPorts). Remote ports $($rule.RemotePorts)."
            if ($rule.ApplicationName) { "  Applies to app $($rule.ApplicationName)." }
            if ($rule.LocalAddresses -ne '*' -or $rule.RemoteAddresses -ne '*') {
                "  Applies to local addresses $($rule.LocalAddresses) and remote addresses $($rule.RemoteAddresses)."
            }
        }
    }
} catch { 'Windows firewall rules could not be checked. Windows may require administrator access.' }
try {
    $entries = & netsh.exe interface portproxy show all
    if ($LASTEXITCODE -ne 0) { throw 'Port forwarding settings unavailable' }
    $matchedPorts = @{}
    foreach ($entry in $entries) {
        if ($entry -notmatch '^\s*(\S+)\s+(\d+)\s+(\S+)\s+(\d+)\s*$') { continue }
        $listenAddress, $listenPort = $Matches[1], [int]$Matches[2]
        $targetAddress, $targetPort = $Matches[3], [int]$Matches[4]
        if ($listenPort -notin $ports -and $targetPort -notin $ports) { continue }
        $matchedPorts[$listenPort] = $true
        $matchedPorts[$targetPort] = $true
        "Windows TCP forwarding rule sends connections from $listenAddress port $listenPort to $targetAddress port $targetPort."
        if ($tcpListening.ContainsKey($listenPort)) {
            $wildcard = if ($listenAddress -match ':') { '::' } else { '0.0.0.0' }
            $listener = $tcpListening[$listenPort] | Where-Object { $_.LocalAddress -in @($listenAddress, $wildcard) }
            if (!$listener) {
                "The forwarding rule exists, but Windows shows no TCP listener at $listenAddress port $listenPort."
            }
        }
    }
    foreach ($port in $ports) {
        if (!$matchedPorts.ContainsKey($port)) { "Windows has no standard TCP forwarding rule using port $port." }
    }
} catch { 'Windows TCP forwarding for ports 53317 and 53327 could not be checked.' }
if (Get-Command Get-NetNatStaticMapping -ErrorAction SilentlyContinue) {
    try {
        $mappings = @(Get-NetNatStaticMapping | Where-Object {
            $_.ExternalPort -in $ports -or $_.InternalPort -in $ports
        })
        if (!$mappings.Count) { 'Windows NAT has no static mappings for ports 53317 or 53327.' }
        foreach ($mapping in $mappings) {
            "Windows NAT $($mapping.Protocol) mapping forwards $($mapping.ExternalIPAddress) port $($mapping.ExternalPort) to $($mapping.InternalIPAddress) port $($mapping.InternalPort)."
        }
    } catch { 'Windows NAT static mappings could not be checked. Windows may require administrator access.' }
}
if ($checkWsl -and !(Get-Command Get-NetFirewallHyperVVMSetting -ErrorAction SilentlyContinue)) {
    'Windows does not provide the command needed to check its WSL firewall (Hyper-V).'
} elseif ($checkWsl) {
    try {
        $wslId = '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}'
        $settings = Get-NetFirewallHyperVVMSetting -PolicyStore ActiveStore -Name $wslId
        if (!$settings) { throw 'WSL firewall settings unavailable' }
        if ($settings.Enabled -eq 'False') {
            'Windows firewall for WSL (Hyper-V) is off.'
        } else {
            $policy = switch ([string]$settings.DefaultInboundAction) {
                'Allow' { 'allows incoming connections by default' }
                'Block' { 'blocks incoming connections by default' }
                default { 'did not report a default for incoming connections' }
            }
            "Windows firewall for WSL (Hyper-V) $policy."
            $rules = @(Get-NetFirewallHyperVRule -PolicyStore ActiveStore -VMCreatorId $wslId)
            foreach ($port in $ports) {
                $matching = @($rules | Where-Object {
                    (Has-Port $_.LocalPorts $port) -or (Has-Port $_.RemotePorts $port)
                })
                if (!$matching.Count) { "The Windows firewall for WSL has no rules that name port $port. General rules may still apply." }
                foreach ($rule in $matching) {
                    $name = if ($rule.DisplayName) { $rule.DisplayName } else { $rule.Name }
                    "WSL firewall rule '$name'. $($rule.Direction) $($rule.Protocol). Action $($rule.Action). Enabled $($rule.Enabled). Local ports $($rule.LocalPorts -join ', '). Remote ports $($rule.RemotePorts -join ', ')."
                }
            }
        }
    } catch { 'Windows firewall for WSL (Hyper-V) could not be fully checked.' }
}
)PS";
    std::string command = script;
    if (diagnostics.wsl) command.insert(0, "$checkWsl = $true\n");
    std::vector<std::string> args{"powershell.exe", "-NoProfile", "-NonInteractive", "-Command", command};
    const RemoteCommandRunner runner;
    auto windows = runner.RunStreaming(args, {}, &cancelled, {}, {15});
#if defined(__linux__)
    if (windows.exit_code == 127 && !cancelled) {
        args.front() = "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe";
        windows = runner.RunStreaming(args, {}, &cancelled, {}, {15});
    }
#endif
    if (windows.exit_code != 0 || windows.output.empty()) {
        findings.push_back("Windows ports and firewall could not be checked. Windows PowerShell may be unavailable.");
        return;
    }
    std::istringstream lines(windows.output);
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("@port ", 0) == 0) {
            std::istringstream entry(line);
            std::string tag, protocol, state;
            unsigned port = 0;
            if (!(entry >> tag >> protocol >> port >> state)) continue;
            const auto row = std::find_if(ports.begin(), ports.end(), [port](const auto& row) {
                return row.system == "Windows host" && row.port == port;
            });
            if (row == ports.end()) continue;
            const auto availability = state == "free" ? PortAvailability::Available
                : state == "in-use" ? PortAvailability::InUse
                : state == "reserved" ? PortAvailability::Reserved : PortAvailability::Unknown;
            if (protocol == "TCP") row->tcp = availability;
            if (protocol == "UDP") row->udp = availability;
            continue;
        }
        if (!line.empty()) findings.push_back(std::move(line));
    }
}

bool IsAutomaticLocalSendAddress(const std::string& address) {
    return address.empty() || address == "0.0.0.0" || Lower(address) == "auto";
}

std::string SelectLanIPv4(const NetworkEnvironment& environment) {
    const InterfaceAddress* best = nullptr;
    int best_score = -1;
    for (const auto& address : environment.addresses) {
        if (!address.usable || !address.ipv4 || address.host_only) continue;
        const bool gateway = HasIPv4DefaultRoute(environment, address);
        const bool physical_lan = IsLanAdapter(address);
        const int score = (address.address.compare(0, 8, "169.254.") == 0 ? 0 : 8) +
            (physical_lan ? 3 : 0) + (gateway ? 2 : 0);
        if (score > best_score) {
            best = &address;
            best_score = score;
        }
    }
    return best ? best->address : "";
}

std::optional<InterfaceAddress> SelectWslNatLanIPv4(const NetworkDiagnostics& network) {
    if (!network.wsl || network.wsl_mode != "nat") return {};
    const auto& host = network.host;
    const InterfaceAddress* best = nullptr;
    for (const auto& address : host.addresses) {
        if (!IsUsefulIPv4(host, address) || !IsLanAdapter(address) ||
            address.prefix < 1 || address.prefix > 31) continue;
        if (HasIPv4DefaultRoute(host, address)) return address;
        if (!best) best = &address;
    }
    return best ? std::optional<InterfaceAddress>{*best} : std::nullopt;
}

std::vector<DiagnosticAddress> DiagnosticAddresses(const NetworkDiagnostics& network,
                                                   const std::string& textlt_ip) {
    struct RankedAddress {
        DiagnosticAddress row;
        int priority;
    };
    std::vector<RankedAddress> ranked;
    const auto is_gateway = [&](const std::string& ip) {
        for (const auto* environment : {&network.host, &network.local}) {
            if (std::any_of(environment->gateways.begin(), environment->gateways.end(),
                [&](const auto& gateway) { return gateway.address == ip; })) return true;
        }
        return false;
    };
    for (const auto* environment : {&network.host, &network.local}) {
        const bool host = environment == &network.host;
        const bool guest = network.wsl && !host;
        const bool docker_host = network.container && host;
        const std::string platform = network.container ? environment->name :
            host || environment->name == "Native Windows" ? "Windows" :
            guest && !network.wsl_distribution.empty() ? network.wsl_distribution :
            environment->name == "macOS" ? "macOS" : "Linux";
        for (const auto& address : environment->addresses) {
            if (!IsUsefulIPv4(*environment, address) ||
                (!docker_host && is_gateway(address.address))) continue;
            const bool routed = HasIPv4DefaultRoute(*environment, address);
            const bool lan = IsLanAdapter(address);
            const bool current = address.address == textlt_ip;
            const int priority = host && lan && routed ? 4 : current ? 3 :
                lan && routed ? 2 : lan || routed ? 1 : 0;
            const auto kind = docker_host ? address.interface_name :
                guest ? "WSL" : lan ? address.type : "Network";
            const bool can_bind = !docker_host && std::any_of(network.local.addresses.begin(), network.local.addresses.end(),
                [&](const auto& local) {
                    return local.ipv4 && local.usable && local.address == address.address;
                });
            ranked.push_back({{address.address, platform + " · " + kind, can_bind, host && !docker_host, docker_host}, priority});
        }
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return left.priority > right.priority;
    });
    std::vector<DiagnosticAddress> result;
    for (auto& entry : ranked) {
        if (std::none_of(result.begin(), result.end(),
            [&](const auto& row) { return row.ip == entry.row.ip; })) {
            result.push_back(std::move(entry.row));
        }
    }
    return result;
}

} // namespace textlt::connect_ui
