#include "connect_workspace.hpp"

#include <algorithm>

#include "json_utils.hpp"

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
    {"localsend_alias", &Settings::localsend_alias},
    {"localsend_interface", &Settings::localsend_interface},
    {"localsend_group", &Settings::localsend_group},
    {"localsend_udp_port", &Settings::localsend_udp_port},
    {"localsend_https_port", &Settings::localsend_https_port},
};

constexpr std::pair<const char*, bool Settings::*> kFlags[] = {
    {"discovery", &Settings::discovery},
    {"localsend", &Settings::localsend},
};

bool IsLoopback(const std::string& address) {
    return address.compare(0, 4, "127.") == 0;
}

} // namespace

void ConnectWorkspace::RefreshLocalIPv4() {
    local_ipv4_ = SelectLanIPv4(InspectLocalNetwork().local);
}

void ConnectWorkspace::RefreshDiagnostics() {
    diagnostics_cancelled_ = true;
    if (diagnostics_worker_.joinable()) diagnostics_worker_.join();
    diagnostics_cancelled_ = false;
    auto snapshot = InspectLocalNetwork();
    local_ipv4_ = SelectLanIPv4(snapshot.local);
    {
        std::lock_guard lock(diagnostics_mutex_);
        diagnostics_ = snapshot;
    }
    if (!snapshot.wsl && snapshot.local.name != "macOS") return;
    diagnostics_worker_ = std::thread([this, snapshot = std::move(snapshot)]() mutable {
        try {
            InspectSupplementalNetwork(snapshot, diagnostics_cancelled_);
        } catch (const Json::exception&) {
            snapshot.host.notice = "Could not read Windows host interface data";
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
    settings_.localsend_interface = address;
    return SaveSettings();
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
    for (const auto& [key, field] : kFlags) {
        const auto value = stored.find(key);
        if (value != stored.end() && value->is_boolean()) settings_.*field = value->get<bool>();
    }
    if (IsLoopback(settings_.listen_ip)) settings_.listen_ip = local_ipv4_;
    if (IsLoopback(settings_.peer_ip)) settings_.peer_ip.clear();
    if (IsLoopback(settings_.discovery_ip)) settings_.discovery_ip.clear();
    if (IsLoopback(settings_.localsend_interface)) {
        settings_.localsend_interface.clear();
    }
}

bool ConnectWorkspace::SaveSettings() {
    auto stored = Json::object();
    for (const auto& [key, field] : kTextSettings) stored[key] = settings_.*field;
    for (const auto& [key, field] : kFlags) stored[key] = settings_.*field;
    return WriteJsonAtomically(localsend_identity_.parent_path() / "connect-settings.json", stored, true);
}

} // namespace textlt::connect_ui
