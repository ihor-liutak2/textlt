#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "remote/remote_connection_config.hpp"

namespace textlt {

struct RemoteOAuthToken {
    std::string provider;
    std::string display_name;
    std::string access_token;
    std::string refresh_token;
    std::string token_type = "Bearer";
    std::string scope;
    std::int64_t expires_at_unix = 0;
    std::string raw_response;

    bool HasUsableToken() const;
};

bool IsCloudRemoteConnectionType(RemoteConnectionType type);
std::string RemoteTokenProviderName(RemoteConnectionType type);
std::filesystem::path ExpandRemoteUserPath(const std::string& path);
std::filesystem::path DefaultRemoteTokenDirectory();
std::filesystem::path DefaultRemoteTokenPath(RemoteConnectionType type, const std::string& stable_name);

class RemoteOAuthTokenStore {
public:
    explicit RemoteOAuthTokenStore(std::filesystem::path token_path);

    const std::filesystem::path& Path() const { return path_; }
    bool Exists() const;
    bool EnsureParentDirectory(std::string& error) const;
    bool Load(RemoteOAuthToken& token, std::string& error) const;
    bool Save(const RemoteOAuthToken& token, std::string& error) const;
    bool Remove(std::string& error) const;

private:
    std::filesystem::path path_;
};

std::string DescribeRemoteOAuthTokenStatus(const RemoteConnectionConfig& config);

} // namespace textlt
