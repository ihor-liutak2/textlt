#pragma once

#include <atomic>
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

struct NetworkDiagnostics {
    NetworkEnvironment local;
    NetworkEnvironment host;
    std::string wsl_mode;
    bool wsl = false;
};

NetworkDiagnostics InspectLocalNetwork();
void InspectSupplementalNetwork(NetworkDiagnostics& diagnostics, const std::atomic<bool>& cancelled);
bool IsAutomaticLocalSendAddress(const std::string& address);
std::string SelectLanIPv4(const NetworkEnvironment& environment);

} // namespace textlt::connect_ui
