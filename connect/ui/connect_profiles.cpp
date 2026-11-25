#include "../src/providers/textlt/socket.hpp"
#include "../src/providers/textlt/discovery.hpp"

#include "connect_workspace.hpp"

#include <algorithm>
#include <charconv>

#include "json_utils.hpp"
#include "textlt/connect/textlt_provider.hpp"

namespace textlt::connect_ui {
namespace {

constexpr int kConnectionVersion = 1;
constexpr std::size_t kMaxConnectionJson = 4096;
constexpr char kConnectionsFile[] = "textlt-connections.json";

std::uint16_t ParseConnectionPort(std::string_view text) {
    if (text.empty()) return 0;
    std::uint16_t port = 0;
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, port);
    return result.ec == std::errc{} && result.ptr == end ? port : 0;
}

bool ValidDeviceName(std::string_view name) {
    return name.find_first_not_of(' ') != std::string_view::npos && connect::detail::ValidDiscoveryName(name);
}

connect::ProviderResult ValidateProfile(NativeConnectionProfile& profile) {
    if (!ValidDeviceName(profile.name)) return {false, "Enter a device name up to 96 bytes."};
    if (!connect::detail::IsLanIPv4(profile.address) || !profile.port) {
        return {false, "Enter a network IPv4 address and a TCP port between 1 and 65535."};
    }
    const auto code = connect::MakeTextLTPairingCode(profile.peer_id, profile.fingerprint);
    const auto identity = code ? connect::ParseTextLTPairingCode(*code) : std::nullopt;
    if (!identity) return {false, "Enter a valid TextLT peer ID and certificate fingerprint."};
    profile.fingerprint = identity->fingerprint;
    return {true, {}};
}

Json EncodeProfile(const NativeConnectionProfile& profile) {
    const auto pairing_code = connect::MakeTextLTPairingCode(profile.peer_id, profile.fingerprint);
    return {{"version", kConnectionVersion}, {"name", profile.name}, {"peer_id", profile.peer_id},
            {"address", profile.address}, {"port", profile.port}, {"fingerprint", profile.fingerprint},
            {"pairing_code", pairing_code.value_or("")}};
}

connect::ProviderResult DecodeProfile(const Json& json, NativeConnectionProfile& profile) {
    if (!json.is_object()) return {false, "Paste a TextLT identity JSON object."};
    const auto version = json.find("version");
    if (version == json.end() || !version->is_number_integer() || *version != kConnectionVersion) {
        return {false, "TextLT requires identity JSON with version 1."};
    }
    const auto string_value = [&json](const char* key) {
        const auto value = json.find(key);
        return value != json.end() && value->is_string() ? value->get<std::string>() : std::string{};
    };
    const auto port = json.find("port");
    if (port == json.end() || !port->is_number_integer() || *port < 1 || *port > 65535) {
        return {false, "The JSON port must be a number between 1 and 65535."};
    }
    profile = {string_value("name"), string_value("peer_id"), string_value("address"),
               port->get<std::uint16_t>(), string_value("fingerprint")};
    const auto validated = ValidateProfile(profile);
    if (!validated.accepted) return validated;
    const auto pairing_code = json.find("pairing_code");
    if (pairing_code == json.end() || !pairing_code->is_string() ||
        pairing_code->get<std::string>() !=
            connect::MakeTextLTPairingCode(profile.peer_id, profile.fingerprint).value_or("")) {
        return {false, "The JSON must include a valid pairing_code matching its peer ID and fingerprint."};
    }
    return {true, {}};
}

} // namespace

NativeConnectionProfile ConnectWorkspace::NativeIdentity() const {
    const auto& identity = native_running_ ? active_settings_ : settings_;
    return {settings_.display_name, identity.local_id,
            identity.listen_ip.empty() ? local_ipv4_ : identity.listen_ip,
            ParseConnectionPort(identity.listen_port),
            connect::TextLTCertificateFingerprint(identity.certificate).value_or("")};
}

connect::ProviderResult ConnectWorkspace::SaveNativeDeviceName(std::string_view name) {
    if (!ValidDeviceName(name)) return {false, "Enter a device name up to 96 bytes."};
    if (settings_.display_name == name) return {true, {}};
    const auto previous = settings_.display_name;
    settings_.display_name = name;
    if (!SaveSettings()) {
        settings_.display_name = previous;
        return {false, "Could not save the device name."};
    }
    return {true, native_running_ ? "Device name saved. Restart TextLT to update discovery."
                                  : "Device name saved."};
}

connect::ProviderResult ConnectWorkspace::SaveNativePeerId(std::string_view peer_id) {
    if (!connect::detail::ValidDiscoveryId(peer_id)) return {false, "Enter a valid TextLT peer ID."};
    if (peer_id == settings_.peer_id) return {false, "Choose an ID different from your peer's ID."};
    if (settings_.local_id == peer_id) return {true, {}};
    const auto previous = settings_.local_id;
    settings_.local_id = peer_id;
    if (!SaveSettings()) {
        settings_.local_id = previous;
        return {false, "Could not save the peer ID."};
    }
    return {true, native_running_ ? "Peer ID saved. Restart TextLT to use it." : "Peer ID saved."};
}

connect::ProviderResult ConnectWorkspace::SaveNativeIdentityPaths(std::string certificate,
                                                                   std::string private_key) {
    if (certificate.empty() || private_key.empty()) return {false, "Enter both PEM paths."};
    const auto fingerprint = connect::TextLTIdentityFingerprint(certificate, private_key);
    if (!fingerprint) return {false, "Could not load a matching certificate and private key from those paths."};

    const auto previous = NativeIdentity().fingerprint;
    std::swap(settings_.certificate, certificate);
    std::swap(settings_.private_key, private_key);
    if (!SaveSettings()) {
        settings_.certificate = std::move(certificate);
        settings_.private_key = std::move(private_key);
        return {false, "Could not save the PEM paths."};
    }
    const bool changed = *fingerprint != previous;
    if (native_running_) {
        return {true, changed ? "PEM paths saved. Restart TextLT, then share the new JSON."
                              : "PEM paths saved. Restart TextLT to use them."};
    }
    return {true, changed ? "PEM paths saved. Share the new identity JSON." : "PEM paths saved."};
}

connect::ProviderResult ConnectWorkspace::RegenerateNativeIdentity(std::string_view name) {
    if (!ValidDeviceName(name)) return {false, "Enter a device name up to 96 bytes."};
    const auto pem = connect::CreateTextLTIdentity(localsend_identity_.parent_path());
    if (!pem) return {false, "Could not create a new TextLT identity."};
    const auto path = pem->u8string();
    auto fingerprint = connect::TextLTCertificateFingerprint(path);
    const auto previous = settings_;
    if (fingerprint) {
        fingerprint->erase(std::remove(fingerprint->begin(), fingerprint->end(), ':'), fingerprint->end());
        settings_.certificate = settings_.private_key = path;
        settings_.local_id = "textlt-" + fingerprint->substr(0, 32);
        settings_.display_name = name;
        if (SaveSettings()) {
            if (native_running_) {
                StopProvider(connect::kTextLTProviderId);
                native_running_ = false;
                active_settings_ = {};
            }
            status_ = "TextLT identity regenerated.";
            return {true, "Identity regenerated. Share the new JSON with your peers."};
        }
    }
    settings_ = previous;
    std::error_code ignored;
    std::filesystem::remove(*pem, ignored);
    return {false, "Could not save the new TextLT identity."};
}

connect::ProviderResult ConnectWorkspace::ExportNativeIdentity(std::string_view address, std::string_view port,
                                                              std::string& json) const {
    auto profile = NativeIdentity();
    profile.address = address;
    profile.port = ParseConnectionPort(port);
    const auto result = ValidateProfile(profile);
    if (!result.accepted) return result;
    json = EncodeProfile(profile).dump(2);
    return {true, {}};
}

void ConnectWorkspace::LoadNativeConnections() {
    const auto settings = LoadJsonObject(localsend_identity_.parent_path() / "connect-settings.json");
    const auto accepted = settings.find("auto_accept_peers");
    if (accepted != settings.end() && accepted->is_array()) {
        for (const auto& peer : *accepted) {
            const auto provider = peer.find("provider");
            const auto id = peer.find("peer_id");
            if (provider != peer.end() && provider->is_string() && id != peer.end() && id->is_string() &&
                !provider->get<std::string>().empty() && !id->get<std::string>().empty()) {
                auto_accept_peers_.emplace(provider->get<std::string>(), id->get<std::string>());
            }
        }
    }
    const auto saved = LoadJsonObject(localsend_identity_.parent_path() / kConnectionsFile);
    const auto version = saved.find("version");
    const auto connections = saved.find("connections");
    if (version == saved.end() || !version->is_number_integer() || *version != kConnectionVersion ||
        connections == saved.end() || !connections->is_array()) return;
    for (const auto& item : *connections) {
        NativeConnectionProfile profile;
        if (DecodeProfile(item, profile).accepted) native_connections_.push_back(std::move(profile));
    }
}

connect::ProviderResult ConnectWorkspace::ImportNativeConnection(std::string_view json, std::size_t& index) {
    if (json.size() > kMaxConnectionJson) return {false, "The identity JSON is too large."};
    auto value = Json::parse(json.begin(), json.end(), nullptr, false);
    NativeConnectionProfile profile;
    const auto result = DecodeProfile(value, profile);
    if (!result.accepted) return result;
    const auto local_fingerprint = connect::TextLTCertificateFingerprint(settings_.certificate);
    if (profile.peer_id == settings_.local_id || profile.fingerprint == local_fingerprint) {
        return {false, "Import the JSON copied from the other TextLT app."};
    }
    auto connections = native_connections_;
    const auto existing = std::find_if(connections.begin(), connections.end(), [&](const auto& entry) {
        return entry.peer_id == profile.peer_id;
    });
    const bool added = existing == connections.end();
    const auto saved_index = static_cast<std::size_t>(existing - connections.begin());
    if (added) connections.push_back(std::move(profile));
    else *existing = std::move(profile);

    auto entries = Json::array();
    for (const auto& entry : connections) entries.push_back(EncodeProfile(entry));
    if (!WriteJsonAtomically(localsend_identity_.parent_path() / kConnectionsFile,
                             {{"version", kConnectionVersion}, {"connections", std::move(entries)}}, true)) {
        return {false, "Could not save the TextLT connection list."};
    }
    native_connections_ = std::move(connections);
    index = saved_index;
    return {true, added ? "Connection saved." : "Connection updated."};
}

connect::ProviderResult ConnectWorkspace::ActivateNativeConnection(std::size_t index) {
    if (index >= native_connections_.size()) return {false, "Choose a saved connection first."};
    const auto& profile = native_connections_[index];
    const Settings previous = settings_;
    const Settings previous_active = active_settings_;
    const bool was_running = native_running_;
    if (was_running) {
        StopProvider(connect::kTextLTProviderId);
        native_running_ = false;
    }
    settings_.peer_ip = profile.address;
    settings_.peer_port = std::to_string(profile.port);
    settings_.peer_id = profile.peer_id;
    settings_.peer_fingerprint = profile.fingerprint;
    StartNative();
    if (native_running_ && SaveSettings()) {
        const PeerKey peer{connect::kTextLTProviderId, profile.peer_id};
        Get({peer.first, peer.second}).name = profile.name;
        Select(peer);
        status_ = "Connecting to " + profile.name + " at " + profile.address + ":" + std::to_string(profile.port);
        return {true, status_};
    }

    const auto failure = native_running_ ? "Could not save the active TextLT connection." : status_;
    if (native_running_) {
        StopProvider(connect::kTextLTProviderId);
        native_running_ = false;
    }
    if (was_running) {
        settings_ = previous_active;
        StartNative();
    }
    settings_ = previous;
    status_ = failure;
    if (was_running && !native_running_) status_ += " The previous connection could not be restarted.";
    return {false, status_};
}

} // namespace textlt::connect_ui
