#pragma once

#include <string>

#include "remote/remote_oauth_token_store.hpp"

namespace textlt {

struct OAuthFlowConfig {
    std::string authorize_url;
    std::string token_url;
    std::string client_id;
    std::string client_secret;
    std::string scope;
    std::string redirect_uri = "http://localhost";
};

struct OAuthTokenExchangeResult {
    bool ok = false;
    RemoteOAuthToken token;
    std::string error;
};

class OAuthFlow {
public:
    OAuthFlow() = default;

    OAuthFlow(const OAuthFlow&) = delete;
    OAuthFlow& operator=(const OAuthFlow&) = delete;

    std::string BuildAuthorizeUrl(const OAuthFlowConfig& config) const;

    std::string ExtractCodeFromRedirectUrl(const std::string& redirect_url) const;

    OAuthTokenExchangeResult ExchangeCodeForToken(
        const OAuthFlowConfig& config,
        const std::string& authorization_code);

    bool SaveToken(
        const OAuthFlowConfig& config,
        const std::string& provider_name,
        const std::string& display_name,
        const OAuthTokenExchangeResult& exchange_result,
        const std::string& token_file_path);
};

} // namespace textlt
