#include "remote/remote_config_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>
#include <utility>

#include "json_utils.hpp"

namespace textlt {
namespace {

std::string GetEnvironmentVariableValue(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

std::filesystem::path DefaultRemoteConfigPath() {
#ifdef _WIN32
    const std::string app_data = GetEnvironmentVariableValue("APPDATA");
    if (!app_data.empty()) {
        return std::filesystem::path(app_data) / "textlt" / "remote_connections.json";
    }
    const std::string user_profile = GetEnvironmentVariableValue("USERPROFILE");
    if (!user_profile.empty()) {
        return std::filesystem::path(user_profile) / "AppData" / "Roaming" / "textlt" /
            "remote_connections.json";
    }
    return "remote_connections.json";
#else
    const std::string home = GetEnvironmentVariableValue("HOME");
    if (home.empty()) {
        return "remote_connections.json";
    }
    return std::filesystem::path(home) / ".config" / "textlt" / "remote_connections.json";
#endif
}

bool IsRemovedCloudConnectionType(std::string type) {
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return type == "google" || type == "google_drive" || type == "google-drive" ||
        type == "microsoft" || type == "microsoft_drive" || type == "microsoft-drive" ||
        type == "onedrive" || type == "one_drive" || type == "sharepoint";
}

RemoteConnectionConfig ParseConnection(const Json& object) {
    RemoteConnectionConfig config;
    config.id = JsonString(object, "id");
    config.name = JsonString(object, "name");
    config.type = RemoteConnectionTypeFromString(JsonString(object, "type", "sftp"));
    config.host = JsonString(object, "host");
    config.port = JsonInt(object, "port", 22);
    if (config.port <= 0) {
        config.port = 22;
    }
    config.user = JsonString(object, "user");
    config.password = JsonString(object, "password");
    config.remote_root = NormalizeRemoteDirectory(JsonString(object, "remote_root", "/"));
    config.auth_mode = JsonString(object, "auth_mode", "auto");
    config.identity_file = JsonString(object, "identity_file", JsonString(object, "private_key_file"));
    config.key_passphrase = JsonString(object, "key_passphrase");
    config.known_hosts_file = JsonString(object, "known_hosts_file");
    config.ssh_config_host = JsonString(object, "ssh_config_host");
    config.token_file = JsonString(object, "token_file");
    config.app_key = JsonString(object, "app_key");
    config.app_secret = JsonString(object, "app_secret");
    config.ftps_tls_mode = JsonString(object, "ftps_tls_mode", "explicit");
    config.ftps_passive = JsonBool(object, "ftps_passive", true);

    if (config.id.empty()) {
        config.id = MakeRemoteConnectionId();
    }
    if (config.name.empty()) {
        config.name = config.ssh_config_host.empty() ? config.host : config.ssh_config_host;
    }
    return config;
}

Json SerializeConnection(const RemoteConnectionConfig& config) {
    Json object = Json::object();
    object["id"] = config.id;
    object["name"] = config.name;
    object["type"] = RemoteConnectionTypeToString(config.type);
    object["host"] = config.host;
    object["port"] = config.port;
    object["user"] = config.user;
    object["password"] = config.password;
    object["remote_root"] = config.remote_root.empty() ? "/" : config.remote_root;
    object["auth_mode"] = config.auth_mode.empty() ? "auto" : config.auth_mode;
    object["identity_file"] = config.identity_file;
    object["key_passphrase"] = config.key_passphrase;
    object["known_hosts_file"] = config.known_hosts_file;
    object["ssh_config_host"] = config.ssh_config_host;
    object["token_file"] = config.token_file;
    object["app_key"] = config.app_key;
    object["app_secret"] = config.app_secret;
    object["ftps_tls_mode"] = config.ftps_tls_mode.empty() ? "explicit" : config.ftps_tls_mode;
    object["ftps_passive"] = config.ftps_passive;
    return object;
}

} // namespace

RemoteConfigStore::RemoteConfigStore(std::filesystem::path path)
    : path_(path.empty() ? DefaultRemoteConfigPath() : std::move(path)) {
    std::string error;
    Load(error);
}

bool RemoteConfigStore::Load(std::string& error) {
    error.clear();
    connections_.clear();

    const Json root = LoadJsonObject(path_);
    active_connection_id_ = JsonString(root, "active_connection_id");

    const auto connections_iter = root.find("connections");
    if (connections_iter == root.end()) {
        NormalizeActiveConnection();
        return true;
    }
    if (!connections_iter->is_array()) {
        error = "remote_connections.json has invalid connections field.";
        return false;
    }

    for (const Json& object : *connections_iter) {
        if (!object.is_object()) {
            continue;
        }
        if (IsRemovedCloudConnectionType(JsonString(object, "type"))) {
            continue;
        }
        RemoteConnectionConfig config = ParseConnection(object);
        if (!config.name.empty() || !config.host.empty() || !config.ssh_config_host.empty() ||
            !config.app_key.empty()) {
            connections_.push_back(std::move(config));
        }
    }
    NormalizeActiveConnection();
    return true;
}

bool RemoteConfigStore::Save(std::string& error) const {
    Json root = Json::object();
    root["active_connection_id"] = active_connection_id_;
    root["connections"] = Json::array();
    for (const RemoteConnectionConfig& config : connections_) {
        root["connections"].push_back(SerializeConnection(config));
    }

    if (!WriteJsonAtomically(path_, root)) {
        error = "Cannot write remote config: " + path_.string();
        return false;
    }
#ifndef _WIN32
    std::error_code permission_error;
    std::filesystem::permissions(
        path_,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        permission_error);
    if (permission_error) {
        error = "Cannot protect remote config permissions: " + path_.string();
        return false;
    }
#endif
    error.clear();
    return true;
}

RemoteConnectionConfig* RemoteConfigStore::FindById(const std::string& id) {
    auto iter = std::find_if(connections_.begin(), connections_.end(), [&](const auto& config) {
        return config.id == id;
    });
    return iter == connections_.end() ? nullptr : &*iter;
}

const RemoteConnectionConfig* RemoteConfigStore::FindById(const std::string& id) const {
    auto iter = std::find_if(connections_.begin(), connections_.end(), [&](const auto& config) {
        return config.id == id;
    });
    return iter == connections_.end() ? nullptr : &*iter;
}

void RemoteConfigStore::SetActiveConnectionId(std::string id) {
    active_connection_id_ = std::move(id);
    NormalizeActiveConnection();
}

RemoteConnectionConfig* RemoteConfigStore::FindActiveConnection() {
    NormalizeActiveConnection();
    return active_connection_id_.empty() ? nullptr : FindById(active_connection_id_);
}

const RemoteConnectionConfig* RemoteConfigStore::FindActiveConnection() const {
    if (connections_.empty()) {
        return nullptr;
    }
    const RemoteConnectionConfig* active = active_connection_id_.empty()
        ? nullptr
        : FindById(active_connection_id_);
    return active ? active : &connections_.front();
}

void RemoteConfigStore::NormalizeActiveConnection() {
    if (connections_.empty()) {
        active_connection_id_.clear();
        return;
    }
    if (active_connection_id_.empty() || !FindById(active_connection_id_)) {
        active_connection_id_ = connections_.front().id;
    }
}

void RemoteConfigStore::AddOrUpdate(RemoteConnectionConfig config) {
    if (config.id.empty()) {
        config.id = MakeRemoteConnectionId();
    }
    if (config.name.empty()) {
        config.name = config.ssh_config_host.empty() ? config.host : config.ssh_config_host;
    }
    config.remote_root = NormalizeRemoteDirectory(config.remote_root);

    const std::string saved_id = config.id;
    RemoteConnectionConfig* existing = FindById(saved_id);
    if (existing) {
        *existing = std::move(config);
        NormalizeActiveConnection();
        return;
    }
    connections_.push_back(std::move(config));
    if (active_connection_id_.empty()) {
        active_connection_id_ = saved_id;
    }
    NormalizeActiveConnection();
}

bool RemoteConfigStore::RemoveById(const std::string& id) {
    const size_t old_size = connections_.size();
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(), [&](const auto& config) {
            return config.id == id;
        }),
        connections_.end());
    const bool removed = connections_.size() != old_size;
    if (removed && active_connection_id_ == id) {
        active_connection_id_.clear();
    }
    NormalizeActiveConnection();
    return removed;
}

} // namespace textlt
