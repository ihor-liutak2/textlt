#include "remote/remote_config_store.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <utility>

#include "json_utils.hpp"

namespace textlt {
namespace {

std::filesystem::path DefaultRemoteConfigPath() {
#ifdef _WIN32
    const char* app_data = std::getenv("APPDATA");
    if (app_data && std::string(app_data).size() > 0) {
        return std::filesystem::path(app_data) / "textlt" / "remote_connections.json";
    }
    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && std::string(user_profile).size() > 0) {
        return std::filesystem::path(user_profile) / "AppData" / "Roaming" / "textlt" /
            "remote_connections.json";
    }
    return "remote_connections.json";
#else
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return "remote_connections.json";
    }
    return std::filesystem::path(home) / ".config" / "textlt" / "remote_connections.json";
#endif
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
    config.remote_root = NormalizeRemoteDirectory(JsonString(object, "remote_root", "/"));
    config.identity_file = JsonString(object, "identity_file");
    config.ssh_config_host = JsonString(object, "ssh_config_host");
    config.account_label = JsonString(object, "account_label");
    config.client_id = JsonString(object, "client_id");
    config.client_secret = JsonString(object, "client_secret");
    config.tenant_id = JsonString(object, "tenant_id");
    config.token_file = JsonString(object, "token_file");
    config.root_folder_id = JsonString(object, "root_folder_id");
    config.site_id = JsonString(object, "site_id");
    config.drive_id = JsonString(object, "drive_id");
    config.app_key = JsonString(object, "app_key");
    config.app_secret = JsonString(object, "app_secret");

    if (config.id.empty()) {
        config.id = MakeRemoteConnectionId();
    }
    if (config.name.empty()) {
        if (!config.account_label.empty()) {
            config.name = config.account_label;
        } else {
            config.name = config.ssh_config_host.empty() ? config.host : config.ssh_config_host;
        }
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
    object["remote_root"] = config.remote_root.empty() ? "/" : config.remote_root;
    object["identity_file"] = config.identity_file;
    object["ssh_config_host"] = config.ssh_config_host;
    object["account_label"] = config.account_label;
    object["client_id"] = config.client_id;
    object["client_secret"] = config.client_secret;
    object["tenant_id"] = config.tenant_id;
    object["token_file"] = config.token_file;
    object["root_folder_id"] = config.root_folder_id;
    object["site_id"] = config.site_id;
    object["drive_id"] = config.drive_id;
    object["app_key"] = config.app_key;
    object["app_secret"] = config.app_secret;
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
    const auto connections_iter = root.find("connections");
    if (connections_iter == root.end()) {
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
        RemoteConnectionConfig config = ParseConnection(object);
        if (!config.name.empty() || !config.host.empty() || !config.ssh_config_host.empty() ||
            !config.account_label.empty() || !config.client_id.empty() || !config.app_key.empty()) {
            connections_.push_back(std::move(config));
        }
    }
    return true;
}

bool RemoteConfigStore::Save(std::string& error) const {
    Json root = Json::object();
    root["connections"] = Json::array();
    for (const RemoteConnectionConfig& config : connections_) {
        root["connections"].push_back(SerializeConnection(config));
    }

    if (!WriteJsonAtomically(path_, root)) {
        error = "Cannot write remote config: " + path_.string();
        return false;
    }
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

void RemoteConfigStore::AddOrUpdate(RemoteConnectionConfig config) {
    if (config.id.empty()) {
        config.id = MakeRemoteConnectionId();
    }
    if (config.name.empty()) {
        if (!config.account_label.empty()) {
            config.name = config.account_label;
        } else {
            config.name = config.ssh_config_host.empty() ? config.host : config.ssh_config_host;
        }
    }
    config.remote_root = NormalizeRemoteDirectory(config.remote_root);

    RemoteConnectionConfig* existing = FindById(config.id);
    if (existing) {
        *existing = std::move(config);
        return;
    }
    connections_.push_back(std::move(config));
}

bool RemoteConfigStore::RemoveById(const std::string& id) {
    const size_t old_size = connections_.size();
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(), [&](const auto& config) {
            return config.id == id;
        }),
        connections_.end());
    return connections_.size() != old_size;
}

} // namespace textlt
