#include "remote/remote_config_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "json_utils.hpp"
#include "remote/remote_oauth_token_store.hpp"

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

constexpr char kSharedConnectionFormat[] = "textlt.remote_connection";

std::string SharedString(const Json& object, const char* field) {
    auto value = object.at(field).get<std::string>();
    if (std::any_of(value.begin(), value.end(), [](unsigned char ch) { return ch < ' ' || ch == 127; })) {
        throw std::invalid_argument(std::string("Invalid connection field: ") + field);
    }
    return value;
}

struct SharedConnection {
    RemoteConnectionConfig config;
    std::optional<RemoteOAuthToken> token;
};

SharedConnection DecodeSharedConnection(std::string_view text) {
    if (text.find('\0') != std::string_view::npos) throw std::invalid_argument("Invalid connection JSON.");
    const auto object = Json::parse(text.begin(), text.end(), [](int depth, Json::parse_event_t, Json&) {
        if (depth > 4) throw std::invalid_argument("Invalid connection JSON.");
        return true;
    });
    if (object.at("format") != kSharedConnectionFormat || !object.at("version").is_number_integer() ||
        object.at("version") != 1) {
        throw std::invalid_argument("Unsupported connection format or version.");
    }
    SharedConnection shared;
    auto& config = shared.config;
    config.name = SharedString(object, "name");
    if (config.name.find_first_not_of(' ') == std::string::npos) {
        throw std::invalid_argument("Connection name is required.");
    }
    config.remote_root = NormalizeRemoteDirectory(SharedString(object, "remote_root"));
    const auto type = SharedString(object, "type");
    if (type == "sftp") {
        config.ssh_config_host = SharedString(object, "ssh_config_host");
        if (config.ssh_config_host.empty() || config.ssh_config_host.front() == '-' ||
            config.ssh_config_host.find_first_of(" /?#@\\*?!") != std::string::npos) {
            throw std::invalid_argument("SFTP connection needs a Host alias from ~/.ssh/config.");
        }
        config.auth_mode = "ssh_config";
    } else if (type == "ftps") {
        config.type = RemoteConnectionType::Ftps;
        config.host = SharedString(object, "host");
        config.user = SharedString(object, "user");
        const auto& port = object.at("port");
        if (config.host.empty() || config.host.find_first_of(" /?#@\\") != std::string::npos ||
            !port.is_number_integer() || port < 1 || port > 65535) {
            throw std::invalid_argument("FTPS needs a host and a port from 1 to 65535.");
        }
        config.port = port.get<int>();
        config.ftps_tls_mode = SharedString(object, "ftps_tls_mode");
        std::transform(config.ftps_tls_mode.begin(), config.ftps_tls_mode.end(), config.ftps_tls_mode.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (config.ftps_tls_mode != "explicit" && config.ftps_tls_mode != "implicit") {
            throw std::invalid_argument("FTPS TLS mode must be explicit or implicit.");
        }
        config.ftps_passive = object.at("ftps_passive").get<bool>();
    } else if (type == "dropbox") {
        config.type = RemoteConnectionType::Dropbox;
        config.app_key = SharedString(object, "app_key");
    } else {
        throw std::invalid_argument("Unsupported connection type.");
    }
    if (const auto secrets = object.find("secrets"); secrets != object.end()) {
        if (!secrets->is_object()) throw std::invalid_argument("Invalid connection secrets.");
        const auto read_secret = [&](const char* key, std::string& value) {
            if (secrets->contains(key)) value = SharedString(*secrets, key);
        };
        read_secret("password", config.password);
        read_secret("key_passphrase", config.key_passphrase);
        read_secret("app_secret", config.app_secret);
        if (const auto oauth = secrets->find("oauth"); oauth != secrets->end()) {
            if (config.type != RemoteConnectionType::Dropbox) {
                throw std::invalid_argument("OAuth credentials are only supported for Dropbox.");
            }
            RemoteOAuthToken token;
            token.provider = RemoteTokenProviderName(config.type);
            token.display_name = config.name;
            token.access_token = SharedString(*oauth, "access_token");
            token.refresh_token = SharedString(*oauth, "refresh_token");
            token.token_type = SharedString(*oauth, "token_type");
            token.scope = SharedString(*oauth, "scope");
            const auto& expires = oauth->at("expires_at_unix");
            if (!expires.is_number_integer() ||
                (!expires.is_number_unsigned() && expires.get<std::int64_t>() < 0) ||
                expires.get<std::uint64_t>() > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) ||
                !token.HasUsableToken()) {
                throw std::invalid_argument("Invalid Dropbox OAuth credentials.");
            }
            token.expires_at_unix = expires.get<std::int64_t>();
            shared.token = std::move(token);
        }
    }
    return shared;
}

} // namespace

std::string EncodeSharedRemoteConnection(const RemoteConnectionConfig& config, bool include_secrets) {
    Json object = {{"format", kSharedConnectionFormat}, {"version", 1}, {"name", config.name},
        {"type", RemoteConnectionTypeToString(config.type)}, {"remote_root", config.remote_root}};
    switch (config.type) {
        case RemoteConnectionType::Sftp:
            object["ssh_config_host"] = config.ssh_config_host;
            break;
        case RemoteConnectionType::Ftps:
            object["host"] = config.host;
            object["port"] = config.port;
            object["user"] = config.user;
            object["ftps_tls_mode"] = config.ftps_tls_mode;
            object["ftps_passive"] = config.ftps_passive;
            break;
        case RemoteConnectionType::Dropbox:
            object["app_key"] = config.app_key;
            break;
    }
    if (include_secrets) {
        auto secrets = Json::object();
        if (!config.password.empty()) secrets["password"] = config.password;
        if (!config.key_passphrase.empty()) secrets["key_passphrase"] = config.key_passphrase;
        if (!config.app_secret.empty()) secrets["app_secret"] = config.app_secret;
        if (config.type == RemoteConnectionType::Dropbox && !config.token_file.empty()) {
            const RemoteOAuthTokenStore token_store(ExpandRemoteUserPath(config.token_file));
            if (token_store.Exists()) {
                RemoteOAuthToken token;
                std::string error;
                if (!token_store.Load(token, error)) throw std::runtime_error(error);
                if (token.HasUsableToken()) {
                    secrets["oauth"] = {{"access_token", token.access_token}, {"refresh_token", token.refresh_token},
                        {"token_type", token.token_type}, {"scope", token.scope}, {"expires_at_unix", token.expires_at_unix}};
                }
            }
        }
        if (!secrets.empty()) object["secrets"] = std::move(secrets);
    }
    auto text = object.dump();
    DecodeSharedConnection(text);
    return text;
}

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

    if (!WriteJsonAtomically(path_, root, true)) {
        error = "Cannot write remote config: " + path_.string();
        return false;
    }
    error.clear();
    return true;
}

bool RemoteConfigStore::ImportSharedConnection(std::string_view json, std::string& new_id, std::string& error) {
    SharedConnection shared;
    try {
        shared = DecodeSharedConnection(json);
    } catch (const std::invalid_argument& reason) {
        error = reason.what();
        return false;
    } catch (const Json::exception&) {
        error = "Clipboard does not contain a valid remote connection.";
        return false;
    }
    auto& config = shared.config;
    std::filesystem::path token_path;
    do {
        config.id = MakeRemoteConnectionId();
        token_path = path_.parent_path() / "remote_tokens" / ("dropbox-" + config.id + ".json");
    } while (FindById(config.id) || (shared.token && RemoteOAuthTokenStore(token_path).Exists()));
    const auto id = config.id;
    const RemoteOAuthTokenStore token_store(token_path);
    if (shared.token) {
        if (!token_store.Save(*shared.token, error)) return false;
        config.token_file = token_path.string();
    }
    auto updated = *this;
    updated.AddOrUpdate(std::move(config));
    if (!updated.Save(error)) {
        std::string cleanup;
        if (shared.token && !token_store.Remove(cleanup)) error += "\n" + cleanup;
        return false;
    }
    *this = std::move(updated);
    new_id = id;
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
