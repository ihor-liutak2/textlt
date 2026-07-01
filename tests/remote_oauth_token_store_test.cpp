#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "remote/remote_connection_config.hpp"
#include "remote/remote_oauth_token_store.hpp"

namespace {

void Fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void Expect(bool value, const std::string& message) {
    if (!value) {
        Fail(message);
    }
}

void ExpectEqual(const std::string& actual, const std::string& expected, const std::string& message) {
    if (actual != expected) {
        Fail(message + " expected [" + expected + "] but got [" + actual + "]");
    }
}

} // namespace

int main() {
    using namespace textlt;

    Expect(IsCloudRemoteConnectionType(RemoteConnectionType::GoogleDrive), "Google Drive must be a cloud type.");
    Expect(IsCloudRemoteConnectionType(RemoteConnectionType::MicrosoftDrive), "Microsoft Drive must be a cloud type.");
    Expect(IsCloudRemoteConnectionType(RemoteConnectionType::Dropbox), "Dropbox must be a cloud type.");
    Expect(!IsCloudRemoteConnectionType(RemoteConnectionType::Sftp), "SFTP must not be an OAuth cloud type.");

    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "textlt_remote_token_store_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(temp_dir, cleanup_error);
    std::filesystem::create_directories(temp_dir);

    const std::filesystem::path token_path = temp_dir / "google-token.json";
    RemoteOAuthTokenStore store(token_path);
    std::string error;
    Expect(!store.Exists(), "Token should not exist before saving.");

    RemoteOAuthToken token;
    token.provider = RemoteTokenProviderName(RemoteConnectionType::GoogleDrive);
    token.account_label = "user@example.com";
    token.access_token = "access";
    token.refresh_token = "refresh";
    token.scope = "drive.file";
    token.expires_at_unix = 123456;

    Expect(store.Save(token, error), "Saving token failed: " + error);
    Expect(store.Exists(), "Token should exist after saving.");

    RemoteOAuthToken loaded;
    Expect(store.Load(loaded, error), "Loading token failed: " + error);
    ExpectEqual(loaded.provider, "Google Drive", "Provider mismatch.");
    ExpectEqual(loaded.account_label, "user@example.com", "Account label mismatch.");
    ExpectEqual(loaded.access_token, "access", "Access token mismatch.");
    ExpectEqual(loaded.refresh_token, "refresh", "Refresh token mismatch.");
    Expect(loaded.HasUsableToken(), "Loaded token should be usable.");

    RemoteConnectionConfig config;
    config.type = RemoteConnectionType::GoogleDrive;
    config.token_file = token_path.string();
    const std::string description = DescribeRemoteOAuthTokenStatus(config);
    Expect(description.find("Token file exists") != std::string::npos, "Description should mention existing token file.");
    Expect(description.find("has access/refresh token") != std::string::npos, "Description should mention usable token.");

    Expect(store.Remove(error), "Removing token failed: " + error);
    Expect(!store.Exists(), "Token should be removed.");
    std::filesystem::remove_all(temp_dir, cleanup_error);
    return 0;
}
