#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "remote/remote_config_store.hpp"
#include "remote/remote_oauth_token_store.hpp"

namespace {

void Fail(const std::string& message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void Expect(bool value, const std::string& message) {
    std::cerr << "Check: " << message << '\n';
    if (!value) {
        Fail(message);
    }
}

void ExpectEqual(const std::string& actual, const std::string& expected, const std::string& message) {
    std::cerr << "Check: " << message << '\n';
    if (actual != expected) {
        Fail(message + " expected [" + expected + "] but got [" + actual + "]");
    }
}

} // namespace

int main() {
    using namespace textlt;

    const std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path() / "textlt_remote_config_store_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(temp_dir, cleanup_error);
    std::filesystem::create_directories(temp_dir);

    const std::filesystem::path config_path = temp_dir / "remote_connections.json";

    {
        RemoteConfigStore store(config_path);
        RemoteConnectionConfig first;
        first.id = "first";
        first.name = "First";
        first.type = RemoteConnectionType::Sftp;
        first.host = "one.example.com";
        first.password = "secret";
        first.auth_mode = "password";
        first.identity_file = "~/.ssh/id_ed25519";
        first.key_passphrase = "key-secret";
        first.known_hosts_file = "~/.ssh/known_hosts";
        store.AddOrUpdate(first);

        RemoteConnectionConfig second;
        second.id = "second";
        second.name = "Second";
        second.type = RemoteConnectionType::Dropbox;
        second.app_key = "app-key";
        store.AddOrUpdate(second);

        RemoteConnectionConfig third;
        third.id = "third";
        third.name = "Third";
        third.type = RemoteConnectionType::Ftps;
        third.host = "ftps.example.com";
        third.port = 990;
        third.ftps_tls_mode = "implicit";
        third.ftps_passive = true;
        store.AddOrUpdate(third);

        ExpectEqual(store.ActiveConnectionId(), "first", "First added connection should become active.");
        store.SetActiveConnectionId("second");
        Expect(store.FindActiveConnection() != nullptr, "Active connection must be found.");
        ExpectEqual(store.FindActiveConnection()->id, "second", "Selected active connection mismatch.");

        std::string error;
        Expect(store.Save(error), "Saving remote config failed: " + error);
    }

    {
        RemoteConfigStore store(config_path);
        ExpectEqual(store.ActiveConnectionId(), "second", "Active connection id must persist.");
        Expect(store.FindActiveConnection() != nullptr, "Persisted active connection must be found.");
        ExpectEqual(store.FindActiveConnection()->name, "Second", "Persisted active connection mismatch.");
        ExpectEqual(store.FindActiveConnection()->app_key, "app-key", "Persisted Dropbox app key mismatch.");
        const RemoteConnectionConfig* ftps = store.FindById("third");
        Expect(ftps != nullptr, "Persisted FTPS connection must be found.");
        Expect(ftps->type == RemoteConnectionType::Ftps, "Persisted FTPS type mismatch.");
        ExpectEqual(ftps->ftps_tls_mode, "implicit", "Persisted FTPS TLS mode mismatch.");
        Expect(ftps->ftps_passive, "Persisted FTPS passive mode mismatch.");

        Expect(store.RemoveById("second"), "Removing active connection failed.");
        ExpectEqual(store.ActiveConnectionId(), "first", "Removing active connection should fall back to the next saved connection.");
        Expect(store.FindActiveConnection() != nullptr, "Fallback SFTP connection must be available.");
        ExpectEqual(store.FindActiveConnection()->password, "secret", "Persisted SFTP password mismatch.");
        ExpectEqual(store.FindActiveConnection()->auth_mode, "password", "Persisted SFTP auth mode mismatch.");
        ExpectEqual(store.FindActiveConnection()->key_passphrase, "key-secret", "Persisted SFTP key passphrase mismatch.");
        ExpectEqual(store.FindActiveConnection()->known_hosts_file, "~/.ssh/known_hosts", "Persisted SFTP known hosts file mismatch.");
        Expect(store.RemoveById("third"), "Removing FTPS connection failed.");
    }

    {
        RemoteConfigStore store(config_path);
        std::string imported_id, error;
        const auto original = *store.FindById("third");
        auto ftps = original;
        ftps.password = "private-password";
        ftps.key_passphrase = "private-passphrase";
        ftps.identity_file = "/local/private-key";
        auto share = EncodeSharedRemoteConnection(ftps);
        Expect(share.find("private") == std::string::npos && share.find("\"id\"") == std::string::npos,
               "Clipboard JSON must omit credentials, local paths, and connection IDs.");
        share.insert(1, R"("id":"third","password":"ignored",)");
        Expect(store.ImportSharedConnection(share, imported_id, error), "Valid FTPS copy should import.");
        Expect(imported_id != "third" && store.Connections().size() == 4, "Import must create a new connection.");
        const auto* added = store.FindById(imported_id);
        Expect(added && added->host == original.host && added->port == original.port &&
               added->ftps_tls_mode == original.ftps_tls_mode && added->ftps_passive == original.ftps_passive &&
               added->password.empty(), "Import must preserve shared FTPS settings only.");
        Expect(store.FindById("third")->host == original.host && store.ActiveConnectionId() == "second",
               "Import must preserve the original connection and current default.");

        auto sftp = *store.FindById("first");
        sftp.ssh_config_host = "office";
        Expect(store.ImportSharedConnection(EncodeSharedRemoteConnection(sftp), imported_id, error),
               "SFTP copy should import.");
        Expect(store.FindById(imported_id)->ssh_config_host == "office" &&
               store.FindById(imported_id)->identity_file.empty(), "SFTP must use the destination's SSH config.");

        auto dropbox = *store.FindById("second");
        dropbox.app_secret = "private-app-secret";
        dropbox.token_file = "/local/token.json";
        Expect(store.ImportSharedConnection(EncodeSharedRemoteConnection(dropbox), imported_id, error),
               "Dropbox copy should import.");
        const auto* cloud = store.FindById(imported_id);
        Expect(cloud && cloud->app_key == dropbox.app_key && cloud->app_secret.empty() &&
               cloud->token_file.empty(), "Dropbox must omit secrets and token paths.");

        for (const auto* invalid : {
                "not JSON",
                R"({"title":"Action","tabs":[]})",
                R"({"format":"textlt.remote_connection","version":2,"type":"sftp","name":"SSH","remote_root":"/","ssh_config_host":"office"})",
                R"({"format":"textlt.remote_connection","version":1,"type":"ftps","name":"FTPS","remote_root":"/","host":"host","port":65536,"user":"","ftps_tls_mode":"implicit","ftps_passive":true})",
                R"({"format":"textlt.remote_connection","version":1,"type":"sftp","name":"SSH","remote_root":"/","ssh_config_host":"-oProxyCommand=bad"})"}) {
            Expect(!store.ImportSharedConnection(invalid, imported_id, error) && !error.empty(),
                   "Invalid clipboard input should be rejected.");
        }
        Expect(store.Connections().size() == 6, "Rejected imports must preserve the connection list.");
        RemoteConfigStore reloaded(config_path);
        Expect(reloaded.Connections().size() == 6 && reloaded.FindById(imported_id),
               "Successful imports should persist.");
        RemoteConfigStore blocked(config_path / "blocked");
        Expect(!blocked.ImportSharedConnection(share, imported_id, error) && blocked.Connections().empty(),
               "A failed save must leave the original store intact.");

        const auto secret_share = EncodeSharedRemoteConnection(ftps, true);
        Expect(secret_share.find("private-password") != std::string::npos &&
               secret_share.find("/local/private-key") == std::string::npos,
               "Opt-in clipboard JSON must include the password and omit local key paths.");
        Expect(store.ImportSharedConnection(secret_share, imported_id, error),
               "Opt-in password JSON should import.");
        const auto* credentialed = store.FindById(imported_id);
        Expect(credentialed && credentialed->id != ftps.id && credentialed->password == ftps.password &&
               credentialed->key_passphrase == ftps.key_passphrase && credentialed->identity_file.empty(),
               "Opt-in credentials must be restored on a separate profile.");

        const RemoteOAuthTokenStore source_token(temp_dir / "source-token.json");
        RemoteOAuthToken token;
        token.access_token = "clipboard-access";
        token.refresh_token = "clipboard-refresh";
        token.scope = "files.content.read";
        token.expires_at_unix = 123456789;
        Expect(source_token.Save(token, error), "Test token should be saved.");
        dropbox.token_file = source_token.Path().string();
        const auto cloud_share = EncodeSharedRemoteConnection(dropbox, true);
        Expect(cloud_share.find("clipboard-access") != std::string::npos &&
               cloud_share.find("source-token.json") == std::string::npos,
               "Opt-in share should include OAuth credentials and omit the originating token path.");
        Expect(store.ImportSharedConnection(cloud_share, imported_id, error), "Dropbox secrets must import.");
        const auto* credentialed_cloud = store.FindById(imported_id);
        Expect(credentialed_cloud && credentialed_cloud->app_secret == dropbox.app_secret &&
               !credentialed_cloud->token_file.empty() && credentialed_cloud->token_file != dropbox.token_file,
               "Imported Dropbox connection must use a fresh token file.");
        RemoteOAuthToken restored_token;
        Expect(RemoteOAuthTokenStore(credentialed_cloud->token_file).Load(restored_token, error) &&
               restored_token.access_token == token.access_token && restored_token.refresh_token == token.refresh_token &&
               restored_token.expires_at_unix == token.expires_at_unix && restored_token.scope == token.scope,
               "The new token file must preserve the exported token and its expiry.");

        RemoteConfigStore blocked_token(temp_dir / "blocked-config.json");
        std::filesystem::create_directory(blocked_token.Path());
        Expect(!blocked_token.ImportSharedConnection(cloud_share, imported_id, error) &&
               blocked_token.Connections().empty(), "A failed save must also roll back a token import.");
        const auto token_dir = std::filesystem::path(credentialed_cloud->token_file).parent_path();
        Expect(std::distance(std::filesystem::directory_iterator(token_dir),
                   std::filesystem::directory_iterator()) == 1, "A failed import must clean up its new token file.");
    }

    {
        std::ofstream legacy(config_path, std::ios::binary);
        legacy << R"({"connections":[{"id":"legacy","name":"Legacy","type":"sftp","host":"legacy.example.com"}]})";
        legacy.close();

        RemoteConfigStore store(config_path);
        ExpectEqual(store.ActiveConnectionId(), "legacy", "Legacy config without active id should select the first connection.");
        Expect(store.FindActiveConnection() != nullptr, "Legacy active connection must be available.");
    }

    {
        std::ofstream legacy(config_path, std::ios::binary);
        legacy << R"({"connections":[{"id":"google","name":"Old Google","type":"google_drive"},{"id":"microsoft","name":"Old Microsoft","type":"onedrive"},{"id":"sftp","name":"Current SFTP","type":"sftp","host":"sftp.example.com"}]})";
        legacy.close();

        RemoteConfigStore store(config_path);
        Expect(store.FindById("google") == nullptr, "Removed Google Drive profiles should be ignored.");
        Expect(store.FindById("microsoft") == nullptr, "Removed Microsoft Drive profiles should be ignored.");
        Expect(store.FindById("sftp") != nullptr, "Supported profiles should still load.");
    }

    std::filesystem::remove_all(temp_dir, cleanup_error);
    return 0;
}
