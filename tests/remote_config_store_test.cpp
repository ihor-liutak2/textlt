#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "remote/remote_config_store.hpp"

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
        second.scope = "files.content.write";
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
        ExpectEqual(store.FindActiveConnection()->scope, "files.content.write", "Persisted cloud scope mismatch.");
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
    }

    {
        std::ofstream legacy(config_path, std::ios::binary);
        legacy << R"({"connections":[{"id":"legacy","name":"Legacy","type":"sftp","host":"legacy.example.com"}]})";
        legacy.close();

        RemoteConfigStore store(config_path);
        ExpectEqual(store.ActiveConnectionId(), "legacy", "Legacy config without active id should select the first connection.");
        Expect(store.FindActiveConnection() != nullptr, "Legacy active connection must be available.");
    }

    std::filesystem::remove_all(temp_dir, cleanup_error);
    return 0;
}
