#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace textlt::connect_ui {

struct InterfaceAddress {
    std::string interface_name;
    std::string address;
    int prefix = -1;
    std::string status;
    std::string origin;
    std::string type;
    bool ipv4 = false;
    bool usable = false;
    bool host_only = false;
};

struct DefaultGateway {
    std::string interface_name;
    std::string address;
};

struct NetworkEnvironment {
    std::string name;
    std::string notice;
    std::vector<InterfaceAddress> addresses;
    std::vector<DefaultGateway> gateways;
};

enum class PortAvailability { Unknown, Available, InUse, Reserved };

struct PortAvailabilityRow {
    std::string system;
    unsigned port;
    PortAvailability tcp = PortAvailability::Unknown;
    PortAvailability udp = PortAvailability::Unknown;
};

struct NetworkDiagnostics {
    NetworkEnvironment local;
    NetworkEnvironment host;
    std::string wsl_mode;
    bool wsl = false;
    bool container = false;
    std::string wsl_distribution;
    bool checking_localsend_ports = false;
    std::vector<PortAvailabilityRow> localsend_ports;
    std::vector<std::string> localsend_port_findings;
};

struct DiagnosticAddress {
    std::string ip;
    std::string label;
    bool can_bind;
    bool windows_host;
    bool docker_host = false;
};

NetworkDiagnostics InspectLocalNetwork();
void InspectSupplementalNetwork(NetworkDiagnostics& diagnostics, const std::atomic<bool>& cancelled);
void InspectLocalSendPorts(NetworkDiagnostics& diagnostics, const std::atomic<bool>& cancelled);
bool IsAutomaticLocalSendAddress(const std::string& address);
std::string SelectLanIPv4(const NetworkEnvironment& environment);
std::optional<InterfaceAddress> SelectWslNatLanIPv4(const NetworkDiagnostics& network);
std::vector<DiagnosticAddress> DiagnosticAddresses(const NetworkDiagnostics& network,
                                                   const std::string& textlt_ip);

} // namespace textlt::connect_ui
