#include "remote/remote_oauth_flow.hpp"

#include <sstream>
#include <string>
#include <vector>

#include "json_utils.hpp"
#include "remote/remote_http_client.hpp"

namespace textlt {
namespace {

std::string UrlDecode(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            int hex = 0;
            std::istringstream hex_stream(value.substr(i + 1, 2));
            if (hex_stream >> std::hex >> hex) {
                result += static_cast<char>(hex);
                i += 2;
            } else {
                result += value[i];
            }
        } else if (value[i] == '+') {
            result += ' ';
        } else {
            result += value[i];
        }
    }
    return result;
}

std::string ExtractQueryParam(const std::string& query, const std::string& key) {
    const std::string marker = key + "=";
    size_t pos = query.find(marker);
    if (pos == std::string::npos) {
        return {};
    }
    pos += marker.size();
    size_t end = query.find('&', pos);
    if (end == std::string::npos) {
        end = query.size();
    }
    return UrlDecode(query.substr(pos, end - pos));
}

} // namespace

std::string OAuthFlow::BuildAuthorizeUrl(const OAuthFlowConfig& config) const {
    std::ostringstream url;
    url << config.authorize_url;
    url << "?client_id=" << RemoteHttpClient::UrlEncode(config.client_id);
    url << "&redirect_uri=" << RemoteHttpClient::UrlEncode(config.redirect_uri);
    url << "&response_type=code";
    if (!config.scope.empty()) {
        url << "&scope=" << RemoteHttpClient::UrlEncode(config.scope);
    }
    return url.str();
}

std::string OAuthFlow::ExtractCodeFromRedirectUrl(const std::string& redirect_url) const {
    size_t query_pos = redirect_url.find('?');
    if (query_pos == std::string::npos) {
        return {};
    }
    std::string query = redirect_url.substr(query_pos + 1);
    return ExtractQueryParam(query, "code");
}

OAuthTokenExchangeResult OAuthFlow::ExchangeCodeForToken(
    const OAuthFlowConfig& config,
    const std::string& authorization_code) {

    std::ostringstream body;
    body << "grant_type=authorization_code"
         << "&code=" << RemoteHttpClient::UrlEncode(authorization_code)
         << "&redirect_uri=" << RemoteHttpClient::UrlEncode(config.redirect_uri)
         << "&client_id=" << RemoteHttpClient::UrlEncode(config.client_id)
         << "&client_secret=" << RemoteHttpClient::UrlEncode(config.client_secret);

    RemoteHttpClient http;
    std::vector<std::string> headers = {
        "Content-Type: application/x-www-form-urlencoded"
    };

    RemoteHttpResponse response = http.Request(
        "POST", config.token_url, headers, body.str(), 30);

    OAuthTokenExchangeResult result;
    if (!response.ok) {
        result.error = "Token exchange failed";
        if (!response.error.empty()) {
            result.error += ": " + response.error;
        }
        if (!response.body.empty()) {
            result.error += "\n" + response.body;
        }
        return result;
    }

    Json parsed = Json::parse(response.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        result.error = "Invalid token response JSON.";
        return result;
    }

    result.ok = true;
    result.token.access_token = JsonString(parsed, "access_token");
    result.token.refresh_token = JsonString(parsed, "refresh_token");
    result.token.token_type = JsonString(parsed, "token_type", "Bearer");
    result.token.scope = JsonString(parsed, "scope");
    result.token.raw_response = response.body;

    const auto expires_iter = parsed.find("expires_in");
    if (expires_iter != parsed.end() && expires_iter->is_number_integer()) {
        result.token.expires_at_unix = static_cast<std::int64_t>(
            std::time(nullptr)) + expires_iter->get<std::int64_t>();
    }

    return result;
}

bool OAuthFlow::SaveToken(
    const OAuthFlowConfig& /*config*/,
    const std::string& provider_name,
    const std::string& display_name,
    const OAuthTokenExchangeResult& exchange_result,
    const std::string& token_file_path) {

    if (!exchange_result.ok || token_file_path.empty()) {
        return false;
    }

    RemoteOAuthToken token = exchange_result.token;
    token.provider = provider_name;
    token.display_name = display_name;

    RemoteOAuthTokenStore store(ExpandRemoteUserPath(token_file_path));
    std::string error;
    if (!store.Save(token, error)) {
        return false;
    }
    return true;
}

} // namespace textlt
