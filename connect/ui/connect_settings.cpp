#include "connect_workspace.hpp"

#include <algorithm>

#include "json_utils.hpp"
#include "textlt/connect/textlt_provider.hpp"

namespace textlt::connect_ui {
namespace {

constexpr std::pair<const char*, std::string Settings::*> kTextSettings[] = {
    {"peer_ip", &Settings::peer_ip},
    {"peer_port", &Settings::peer_port},
    {"peer_id", &Settings::peer_id},
    {"peer_fingerprint", &Settings::peer_fingerprint},
    {"listen_ip", &Settings::listen_ip},
    {"listen_port", &Settings::listen_port},
    {"discovery_ip", &Settings::discovery_ip},
    {"local_id", &Settings::local_id},
    {"display_name", &Settings::display_name},
    {"certificate", &Settings::certificate},
    {"private_key", &Settings::private_key},
    {"localsend_peer_endpoint", &Settings::localsend_peer_endpoint},
    {"localsend_alias", &Settings::localsend_alias},
    {"localsend_interface", &Settings::localsend_interface},
    {"localsend_group", &Settings::localsend_group},
    {"localsend_udp_port", &Settings::localsend_udp_port},
    {"localsend_https_port", &Settings::localsend_https_port},
};

bool IsLoopback(const std::string& address) {
    return address.compare(0, 4, "127.") == 0;
}

} // namespace

void ConnectWorkspace::RefreshLocalIPv4() {
    local_ipv4_ = SelectLanIPv4(InspectLocalNetwork().local);
}

void ConnectWorkspace::RefreshDiagnostics(bool test_localsend_ports) {
    if (Diagnostics().checking_localsend_ports) return;
    diagnostics_cancelled_ = true;
    if (diagnostics_worker_.joinable()) diagnostics_worker_.join();
    diagnostics_cancelled_ = false;
    auto snapshot = InspectLocalNetwork();
    snapshot.checking_localsend_ports = test_localsend_ports;
    local_ipv4_ = SelectLanIPv4(snapshot.local);
    {
        std::lock_guard lock(diagnostics_mutex_);
        snapshot.localsend_ports = diagnostics_.localsend_ports;
        snapshot.localsend_port_findings = diagnostics_.localsend_port_findings;
        diagnostics_ = snapshot;
    }
    if (!test_localsend_ports && !snapshot.wsl && !snapshot.container && snapshot.local.name != "macOS") return;
    diagnostics_worker_ = std::thread([this, snapshot = std::move(snapshot)]() mutable {
        try {
            InspectSupplementalNetwork(snapshot, diagnostics_cancelled_);
        } catch (const Json::exception&) {
            snapshot.host.notice = "Could not read Windows host interface data";
        }
        if (diagnostics_cancelled_) return;
        if (snapshot.checking_localsend_ports) {
            {
                std::lock_guard lock(diagnostics_mutex_);
                diagnostics_ = snapshot;
            }
            redraw_();
            InspectLocalSendPorts(snapshot, diagnostics_cancelled_);
            snapshot.checking_localsend_ports = false;
        }
        if (diagnostics_cancelled_) return;
        {
            std::lock_guard lock(diagnostics_mutex_);
            diagnostics_ = std::move(snapshot);
        }
        redraw_();
    });
}

NetworkDiagnostics ConnectWorkspace::Diagnostics() const {
    std::lock_guard lock(diagnostics_mutex_);
    return diagnostics_;
}

bool ConnectWorkspace::UseLocalSendAddress(const std::string& address) {
    const auto network = InspectLocalNetwork();
    if (std::none_of(network.local.addresses.begin(), network.local.addresses.end(), [&](const auto& entry) {
        return entry.ipv4 && entry.usable && entry.address == address;
    })) return false;
    auto previous = settings_.localsend_interface;
    settings_.localsend_interface = address;
    if (SaveSettings()) return true;
    settings_.localsend_interface = std::move(previous);
    return false;
}

void ConnectWorkspace::LoadSettings() {
    RefreshLocalIPv4();
    settings_.listen_ip = local_ipv4_;
    const auto path = localsend_identity_.parent_path() / "connect-settings.json";
    const auto stored = LoadJsonObject(path);
    for (const auto& [key, field] : kTextSettings) {
        const auto value = stored.find(key);
        if (value != stored.end() && value->is_string()) settings_.*field = value->get<std::string>();
    }
    const auto discovery = stored.find("discovery");
    if (discovery != stored.end() && discovery->is_boolean()) settings_.discovery = discovery->get<bool>();
    if (settings_.listen_port.empty()) settings_.listen_port = kDefaultNativePort;
    if (settings_.peer_port.empty()) {
        // Older versions cleared the port when switching from a manual address to discovery.
        if (!settings_.peer_id.empty()) settings_.peer_ip.clear();
        settings_.peer_port = kDefaultNativePort;
    }
    if (settings_.localsend_udp_port.empty()) settings_.localsend_udp_port = kDefaultLocalSendPort;
    if (settings_.localsend_https_port.empty()) settings_.localsend_https_port = kDefaultLocalSendPort;
    if (IsLoopback(settings_.listen_ip)) settings_.listen_ip = local_ipv4_;
    if (IsLoopback(settings_.peer_ip)) settings_.peer_ip.clear();
    if (IsLoopback(settings_.discovery_ip)) settings_.discovery_ip.clear();
    if (IsLoopback(settings_.localsend_interface)) {
        settings_.localsend_interface.clear();
    }
    std::optional<std::string> fingerprint;
    if (settings_.certificate.empty() && settings_.private_key.empty()) {
        const auto pem = localsend_identity_.parent_path() / "textlt-identity.pem";
        fingerprint = connect::EnsureTextLTIdentity(pem);
        if (fingerprint) settings_.certificate = settings_.private_key = pem.u8string();
    } else if (!settings_.certificate.empty()) {
        fingerprint = connect::TextLTCertificateFingerprint(settings_.certificate);
    }
    if (fingerprint) {
        fingerprint->erase(std::remove(fingerprint->begin(), fingerprint->end(), ':'), fingerprint->end());
        if (settings_.local_id.empty()) settings_.local_id = "textlt-" + fingerprint->substr(0, 32);
        if (settings_.display_name.empty()) settings_.display_name = "TextLT " + fingerprint->substr(0, 6);
    }
}

bool ConnectWorkspace::SaveSettings() {
    auto stored = Json::object();
    for (const auto& [key, field] : kTextSettings) stored[key] = settings_.*field;
    stored["discovery"] = settings_.discovery;
    stored["auto_accept_peers"] = Json::array();
    for (const auto& peer : auto_accept_peers_) {
        stored["auto_accept_peers"].push_back({{"provider", peer.first}, {"peer_id", peer.second}});
    }
    return WriteJsonAtomically(localsend_identity_.parent_path() / "connect-settings.json", stored, true);
}

} // namespace textlt::connect_ui
