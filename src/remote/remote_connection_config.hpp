#pragma once

#include <string>

namespace textlt {

enum class RemoteConnectionType {
    Sftp,
    GoogleDrive,
    MicrosoftDrive,
    Dropbox,
};

struct RemoteConnectionConfig {
    std::string id;
    std::string name;
    RemoteConnectionType type = RemoteConnectionType::Sftp;
    std::string host;
    int port = 22;
    std::string user;
    std::string password;
    std::string remote_root = "/";
    std::string auth_mode = "auto";
    std::string identity_file;
    std::string key_passphrase;
    std::string known_hosts_file;
    std::string ssh_config_host;

    std::string client_id;
    std::string client_secret;
    std::string tenant_id;
    std::string token_file;
    std::string root_folder_id;
    std::string site_id;
    std::string drive_id;
    std::string app_key;
    std::string app_secret;
    std::string scope;
};

std::string RemoteConnectionTypeToString(RemoteConnectionType type);
RemoteConnectionType RemoteConnectionTypeFromString(const std::string& value);
std::string MakeRemoteConnectionId();
std::string NormalizeRemoteDirectory(std::string path);
std::string JoinRemotePath(const std::string& directory, const std::string& name);
std::string RemoteParentPath(const std::string& path);
std::string RemoteBaseName(const std::string& path);

} // namespace textlt
